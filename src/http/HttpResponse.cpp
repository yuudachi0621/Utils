#include "HttpResponse.h"
#include "FileUtil.h"
#include "log.h"
#include "URLEncode.h"
#include "stringUtil.h"
#include "sqlConnPool.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <ctime>

using namespace util;

namespace {
std::string StatusText(int code)
{
    const auto it = g_httpStatusCodeMap.find(code);
    return it == g_httpStatusCodeMap.end() ? std::string("Unknown") : it->second;
}

bool IsHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

bool DecodePath(const std::string& input, std::string& output)
{
    output.clear();
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];
        if (c == '%')
        {
            if (i + 2 >= input.size() || !IsHex(input[i + 1]) || !IsHex(input[i + 2]))
                return false;
            output.push_back(static_cast<char>((HexValue(input[i + 1]) << 4) | HexValue(input[i + 2])));
            i += 2;
        }
        else
        {
            output.push_back(c);
        }
    }
    return true;
}

enum class RangeParseResult
{
    None,
    Valid,
    Unsatisfiable,
};

std::string_view TrimView(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

bool ParseSizeValue(std::string_view value, size_t& result)
{
    if (value.empty())
        return false;

    const auto begin  = value.data();
    const auto end    = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

// 当前只实现浏览器视频拖拽最常用的单范围请求：bytes=start-end。
RangeParseResult ParseRangeHeader(std::string_view value, size_t fileSize, size_t& start, size_t& end)
{
    constexpr std::string_view prefix = "bytes=";
    if (value.size() < prefix.size())
        return RangeParseResult::None;

    std::string unit(value.substr(0, prefix.size()));
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (unit != prefix)
        return RangeParseResult::None;

    std::string_view spec = TrimView(value.substr(prefix.size()));
    if (spec.empty() || spec.find(',') != std::string_view::npos)
        return RangeParseResult::None;

    const size_t dash = spec.find('-');
    if (dash == std::string_view::npos)
        return RangeParseResult::None;

    const std::string_view left  = TrimView(spec.substr(0, dash));
    const std::string_view right = TrimView(spec.substr(dash + 1));

    if (left.empty())
    {
        size_t suffixLength = 0;
        if (!ParseSizeValue(right, suffixLength))
            return RangeParseResult::None;
        if (suffixLength == 0 || fileSize == 0)
            return RangeParseResult::Unsatisfiable;

        start = suffixLength >= fileSize ? 0 : fileSize - suffixLength;
        end   = fileSize - 1;
        return RangeParseResult::Valid;
    }

    if (!ParseSizeValue(left, start) || fileSize == 0 || start >= fileSize)
        return RangeParseResult::Unsatisfiable;

    if (right.empty())
    {
        end = fileSize - 1;
        return RangeParseResult::Valid;
    }

    if (!ParseSizeValue(right, end) || start > end)
        return RangeParseResult::Unsatisfiable;

    if (end >= fileSize)
        end = fileSize - 1;

    return RangeParseResult::Valid;
}

bool HasParentSegment(const std::string& path)
{
    size_t begin = 0;
    while (begin <= path.size())
    {
        const size_t end          = path.find('/', begin);
        const std::string segment = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (segment == ".." || segment == ".")
            return true;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return false;
}
} // namespace

class sqlRAII
{
public:
    explicit sqlRAII(MYSQL* sql)
        : m_sql(sql)
    {
    }

    ~sqlRAII()
    {
        if (m_sql)
            sqlConnPool::Instance().FreeConn(m_sql);
    }

private:
    MYSQL* m_sql;
};

void HttpResponse::Init(HttpRequestState* request)
{
    if (m_mmf)
        m_mmf->unmap();
    m_mmf.reset();
    m_fileOffset = 0;
    m_fileLength = 0;

    m_response.reset();
    m_request = request;

    // 设置默认头部
    PrepareCommonHeaders();
}

// 按 HTTP 报文顺序构建：状态行、响应头、空行、正文；HEAD/204 等只保留头部。
std::string HttpResponse::BuildResponse()
{
    HttpResponsePacket packet = BuildResponsePacket();
    std::string response = std::move(packet.header);

    if (packet.file)
    {
        response.reserve(response.size() + packet.fileLength);
        response.append(packet.file->data() + packet.fileOffset, packet.fileLength);
    }
    else
    {
        response.reserve(response.size() + packet.body.size());
        response += packet.body;
    }

    return response;
}

// 生成零拷贝响应包：文件正文只携带 mmap 指针、偏移和长度，不复制到 std::string。
HttpResponsePacket HttpResponse::BuildResponsePacket()
{
    HttpResponsePacket packet;

    // HEAD、1xx、204 和 304 不发送正文，但保留对应的响应头。
    const bool suppressBody =
        !m_request || m_request->method == "HEAD" || m_response.code / 100 == 1 || m_response.code == 204 || m_response.code == 304;

    packet.header += m_response.version;
    packet.header += ' ';
    packet.header += std::to_string(m_response.code);
    packet.header += ' ';
    packet.header += StatusText(m_response.code);
    packet.header += "\r\n";

    for (const auto& [key, value] : m_response.headers)
    {
        packet.header += key;
        packet.header += ": ";
        packet.header += value;
        packet.header += "\r\n";
    }
    packet.header += "\r\n";

    if (!suppressBody)
    {
        if (m_mmf && m_fileLength > 0)
        {
            packet.file       = m_mmf;
            packet.fileOffset = m_fileOffset;
            packet.fileLength = m_fileLength;
        }
        else
        {
            packet.body = m_response.content;
        }
    }

    return packet;
}

int HttpResponse::GetStatusCode() const
{
    return m_response.code;
}

// 方法分发表：每个 HTTP 方法对应一个独立 Handler，未知方法统一返回 405。
void HttpResponse::HandleRequest()
{
    try
    {
        // 方法分发集中在此处，新增方法时需要同时更新 Allow 头。
        if (!m_request || !m_request->valid)
        {
            SendError(400, "Bad Request");
        }
        else if (m_request->method == "GET")
        {
            HandleGet();
        }
        else if (m_request->method == "HEAD")
        {
            HandleHead();
        }
        else if (m_request->method == "POST")
        {
            HandlePost();
        }
        else if (m_request->method == "PUT")
        {
            HandlePut();
        }
        else if (m_request->method == "PATCH")
        {
            HandlePatch();
        }
        else if (m_request->method == "DELETE")
        {
            HandleDelete();
        }
        else if (m_request->method == "OPTIONS")
        {
            HandleOptions();
        }
        else if (m_request->method == "TRACE")
        {
            HandleTrace();
        }
        else
        {
            // 对未实现的方法返回标准的 Allow 头，便于客户端自动发现能力。
            AddHeader("Allow", "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS, TRACE");
            SendError(405, "Method Not Allowed");
        }
    } catch (const std::exception& e)
    {
        SendError(500, "Server Error: " + std::string(e.what()));
    }
}

void HttpResponse::AddHeader(const std::string& key, const std::string& value)
{
    m_response.headers[key] = value;
}

void HttpResponse::SetContentType(const std::string& type)
{
    AddHeader("Content-Type", type);
}

// 静态文件发送：支持单段 Range，返回 200/206/416，并始终暴露 Accept-Ranges。
void HttpResponse::SendFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        SendError(404, "Not Found");
        return;
    }

    const std::streamoff sizeValue = static_cast<std::streamoff>(file.tellg());
    if (sizeValue < 0)
    {
        SendError(500, "Unable to read file size");
        return;
    }

    const size_t fileSize = static_cast<size_t>(sizeValue);
    SetContentType(GetMimeType(path));
    AddHeader("Accept-Ranges", "bytes");

    size_t rangeStart                  = 0;
    size_t rangeEnd                    = 0;
    const RangeParseResult rangeResult = ParseRangeHeader(
        GetRequestHeader("range"),
        fileSize,
        rangeStart,
        rangeEnd);

    if (rangeResult == RangeParseResult::Unsatisfiable)
    {
        m_response.code = 416;
        AddHeader("Content-Range", "bytes */" + std::to_string(fileSize));
        AddHeader("Content-Length", "0");
        return;
    }

    if (m_mmf)
        m_mmf->unmap();
    m_fileOffset = 0;
    m_fileLength = 0;
    m_response.content.clear();

    if (fileSize == 0)
    {
        AddHeader("Content-Length", "0");
        return;
    }

    auto mappedFile = std::make_shared<MMapFile>();
    if (!mappedFile->Init(path))
    {
        SendError(500, "Unable to map file");
        return;
    }
    m_mmf = std::move(mappedFile);

    if (rangeResult == RangeParseResult::Valid)
    {
        m_response.code = 206;
        m_fileOffset    = rangeStart;
        m_fileLength    = rangeEnd - rangeStart + 1;
        AddHeader("Content-Range",
                  "bytes " + std::to_string(rangeStart) + "-" + std::to_string(rangeEnd) + "/" + std::to_string(fileSize));
    }
    else
    {
        m_fileOffset = 0;
        m_fileLength = m_mmf->size();
    }

    AddHeader("Content-Length", std::to_string(m_fileLength));
}

void HttpResponse::SendError(int code, const std::string& message)
{
    m_response.code = code;

    nlohmann::json error = {
        {"error", StatusText(code)},
        {"message", message}};

    SendJSON(error);
}

void HttpResponse::SendJSON(const nlohmann::json& data)
{
    m_response.content = data.dump();

    SetContentType("application/json");
    AddHeader("Content-Length", std::to_string(m_response.content.size()));
}

void HttpResponse::SendText(const std::string& text, const std::string& type)
{
    m_response.content = text;
    SetContentType(type);
    AddHeader("Content-Length", std::to_string(m_response.content.size()));
}

// GET：解析资源路径，存在则返回文件；不存在返回 404。
void HttpResponse::HandleGet()
{
    std::string filePath;
    if (!BuildResourcePath(filePath) || !FileUtil::IsFileExist(filePath))
    {
        SendError(404, "Not Found");
        return;
    }
    SendFile(filePath);
}

// HEAD：复用 GET 的全部响应头，正文由 BuildResponse 抑制。
void HttpResponse::HandleHead()
{
    // HEAD 复用 GET 的响应头，BuildResponse 会抑制正文。
    HandleGet();
}

// POST：支持表单和 JSON，分别映射到注册/登录业务。
void HttpResponse::HandlePost()
{
    if (!m_request)
        return;

    bool result            = false;
    const std::string path = m_request->path;
    if (path != "/register" && path != "/login")
    {
        SendError(404, "Not Found");
        return;
    }

    // 表单和 JSON 共用一个业务入口，后续扩展其他媒体类型时只需增加分支。
    const std::string contentType = GetRequestContentType();
    if (contentType == "application/x-www-form-urlencoded")
    {
        const auto user        = ParseBody();
        const auto nameIt      = user.find("username");
        const auto pwdIt       = user.find("password");
        const std::string name = nameIt == user.end() ? std::string() : nameIt->second;
        const std::string pwd  = pwdIt == user.end() ? std::string() : pwdIt->second;

        result = path == "/register" ? RegisterUser(name, pwd) : LoginVerify(name, pwd);
    }
    else if (contentType == "application/json")
    {
        try
        {
            const nlohmann::json obj = nlohmann::json::parse(m_request->body);
            const std::string name   = obj.value("username", "");
            const std::string pwd    = obj.value("password", "");
            result                   = path == "/register" ? RegisterUser(name, pwd) : LoginVerify(name, pwd);
        } catch (const nlohmann::json::exception&)
        {
            SendError(400, "Invalid JSON body");
            return;
        }
    }
    else
    {
        SendError(415, "Unsupported Media Type");
        return;
    }

    if (result)
        SendFile(g_resourcePath + "/welcome.html");
    else
        SendFile(g_resourcePath + "/error.html");
}

// PUT：创建或覆盖资源；已存在返回 204，新建返回 201。
void HttpResponse::HandlePut()
{
    std::string filePath;
    if (!BuildResourcePath(filePath))
    {
        SendError(403, "Forbidden");
        return;
    }

    const bool existed = FileUtil::IsFileExist(filePath);
    if (!WriteResource(filePath, false))
    {
        SendError(500, "Unable to write resource");
        return;
    }

    m_response.code = existed ? 204 : 201;
    AddHeader("Content-Length", "0");
}

// PATCH：只更新已有资源，不创建新文件。
void HttpResponse::HandlePatch()
{
    std::string filePath;
    if (!BuildResourcePath(filePath))
    {
        SendError(403, "Forbidden");
        return;
    }

    if (!FileUtil::IsFileExist(filePath))
    {
        SendError(404, "Not Found");
        return;
    }

    if (!WriteResource(filePath, true))
    {
        SendError(500, "Unable to update resource");
        return;
    }

    m_response.code = 204;
    AddHeader("Content-Length", "0");
}

// DELETE：删除已有资源，成功返回 204。
void HttpResponse::HandleDelete()
{
    std::string filePath;
    if (!BuildResourcePath(filePath))
    {
        SendError(403, "Forbidden");
        return;
    }

    if (!FileUtil::IsFileExist(filePath))
    {
        SendError(404, "Not Found");
        return;
    }

    if (std::remove(filePath.c_str()) != 0)
    {
        SendError(500, "Unable to delete resource");
        return;
    }

    m_response.code = 204;
    AddHeader("Content-Length", "0");
}

// OPTIONS：返回 Allow 列表，供客户端探测服务器支持的方法。
void HttpResponse::HandleOptions()
{
    m_response.code = 204;
    AddHeader("Allow", "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS, TRACE");
    AddHeader("Content-Length", "0");
}

// TRACE：回显原始请求，主要用于调试。
void HttpResponse::HandleTrace()
{
    if (!m_request)
        return;

    std::string trace = m_request->comleteMessage;
    trace += m_request->body;
    SendText(trace, "message/http; charset=utf-8");
}

// 将 URL path 安全映射到资源根目录，阻止目录穿越和 Windows 特殊路径。
bool HttpResponse::BuildResourcePath(std::string& filePath) const
{
    if (!m_request || m_request->path.empty() || m_request->path[0] != '/')
        return false;

    std::string decodedPath;
    if (!DecodePath(m_request->path, decodedPath))
        return false;

    if (decodedPath.find('\0') != std::string::npos || decodedPath.find('\\') != std::string::npos || decodedPath.find(':') != std::string::npos || HasParentSegment(decodedPath))
    {
        return false;
    }

    if (decodedPath == "/")
    {
        decodedPath = "/index.html";
    }
    else if (g_localHTML_Map.count(decodedPath) != 0)
    {
        decodedPath += ".html";
    }

    filePath = g_resourcePath + decodedPath;
    return true;
}

bool HttpResponse::WriteResource(const std::string& filePath, bool requireExisting)
{
    if (requireExisting && !FileUtil::IsFileExist(filePath))
        return false;

    const size_t slash = filePath.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        const std::string parent = filePath.substr(0, slash);
        if (!parent.empty() && !FileUtil::CreateFolder(parent))
            return false;
    }

    // PUT 和 PATCH 共用写入逻辑；PATCH 在调用前已经保证目标文件存在。
    util::FileUtil::WriteFile(filePath, m_request ? m_request->body.data() : "", m_request ? m_request->body.size() : 0);
    return FileUtil::IsFileExist(filePath);
}

std::string HttpResponse::GetRequestContentType() const
{
    std::string value      = GetRequestHeader("content-type");
    const size_t semicolon = value.find(';');
    if (semicolon != std::string::npos)
        value.resize(semicolon);

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());

    return value;
}

std::string HttpResponse::GetRequestHeader(const std::string& key) const
{
    if (!m_request)
        return std::string();

    const auto it = m_request->headers.find(key);
    return it == m_request->headers.end() ? std::string() : it->second;
}

// 公共响应头：Server/Date 以及根据请求决定的连接复用策略。
void HttpResponse::PrepareCommonHeaders()
{
    AddHeader("Server", "IOCP HttpServer/1.1");
    AddHeader("Date", GetCurrentHttpDate());

    if (m_request && m_request->keep_alive)
    {
        AddHeader("Connection", "keep-alive");
        AddHeader("Keep-Alive", "timeout=120, max=100");
    }
    else
    {
        AddHeader("Connection", "close");
    }
}

// 解析 application/x-www-form-urlencoded 请求体为键值映射。
std::map<std::string, std::string> HttpResponse::ParseBody()
{
    std::map<std::string, std::string> result;
    if (!m_request || m_request->body.empty())
        return result;

    const std::vector<std::string> pairs = util::StringUtil::Split(m_request->body, "&");
    for (const auto& pair : pairs)
    {
        const size_t pos       = pair.find('=');
        std::string key        = URLEncode::decode(pair.substr(0, pos));
        std::string value      = (pos == std::string::npos) ? std::string() : URLEncode::decode(pair.substr(pos + 1));
        result[std::move(key)] = std::move(value);
    }
    return result;
}

bool HttpResponse::LoginVerify(const std::string& name, const std::string& pwd)
{
    if (name.empty() || pwd.empty())
    {
        LOG_DEBUG("LoginVerify: empty name or pwd");
        return false;
    }

    MYSQL* sql = sqlConnPool::Instance().GetFreeConn();
    if (nullptr == sql)
        return false;
    sqlRAII raii(sql);

    std::string query = util::StringUtil::Format("SELECT password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    if (mysql_query(sql, query.data()))
    {
        LOG_DEBUG("LoginVerify: query failed");
        return false;
    }

    MYSQL_RES* res = mysql_store_result(sql);
    if (!res)
    {
        LOG_DEBUG("LoginVerify: no such user");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row)
    {
        mysql_free_result(res);
        LOG_DEBUG("LoginVerify: user not found");
        return false;
    }

    const std::string dbPassword(row[0]);
    const bool success = (pwd == dbPassword); // TODO: 生产环境应比较密码哈希。

    mysql_free_result(res);
    LOG_DEBUG("LoginVerify: %s", success ? "success" : "wrong password");
    return success;
}

bool HttpResponse::RegisterUser(const std::string& name, const std::string& pwd)
{
    if (name.empty() || pwd.empty())
    {
        LOG_DEBUG("RegisterUser: empty name or pwd");
        return false;
    }

    MYSQL* sql = sqlConnPool::Instance().GetFreeConn();
    if (nullptr == sql)
        return false;
    sqlRAII raii(sql);

    std::string checkQuery = util::StringUtil::Format("SELECT username FROM user WHERE username='%s' LIMIT 1", name.c_str());
    if (mysql_query(sql, checkQuery.data()))
    {
        LOG_DEBUG("RegisterUser: check query failed");
        return false;
    }

    MYSQL_RES* res = mysql_store_result(sql);
    if (res && mysql_fetch_row(res))
    {
        mysql_free_result(res);
        LOG_DEBUG("RegisterUser: username already exists");
        return false;
    }
    mysql_free_result(res);

    std::string insertQuery = util::StringUtil::Format("INSERT INTO user(username, password) VALUES('%s', '%s')", name.c_str(), pwd.c_str());
    if (mysql_query(sql, insertQuery.data()))
    {
        LOG_DEBUG("RegisterUser: insert failed");
        return false;
    }

    LOG_DEBUG("RegisterUser: success");
    return true;
}

std::string HttpResponse::GetCurrentHttpDate() const
{
    char buffer[64]  = {0};
    const time_t now = time(nullptr);

#ifdef _MSC_VER
    std::tm tm{};
    gmtime_s(&tm, &now);
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
#else
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
#endif
    return buffer;
}

std::string HttpResponse::GetMimeType(const std::string& path) const
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const auto it = g_contentTypeMap.find(ext);
    return it == g_contentTypeMap.end() ? "application/octet-stream" : it->second;
}
