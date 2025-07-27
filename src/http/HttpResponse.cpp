#include "HttpResponse.h"
#include "FileUtil.h"
#include "MMapFile.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace util;

void HttpResponse::Init(HttpRequestState* request)
{
    m_mmf.unmap();
    m_response.reset();
    m_request = request;

    // 设置默认头部
    PrepareCommonHeaders();
}

std::string HttpResponse::BuildResponse()
{
    std::ostringstream oss;

    // 状态行
    oss << m_response.version << " "
        << m_response.code << " "
        << g_httpStatusCodeMap.at(m_response.code) << "\r\n";

    // 头部
    for (const auto& [key, value] : m_response.headers)
    {
        oss << key << ": " << value << "\r\n";
    }
    oss << "\r\n"; // 头部结束空行

    // 内容
    if (!m_response.content.empty())
    {
        oss.write(m_response.content.data(), m_response.content.size());
    }
    return oss.str();
}

int HttpResponse::GetStatusCode() const
{
    return m_response.code;
}

void HttpResponse::HandleRequest()
{
    // 使用线程局部静态变量（每个线程独立拷贝）
    static thread_local const std::unordered_map<std::string, std::function<void(HttpResponse*)>> METHODS = {
        {"GET", [](HttpResponse* self) { self->HandleGet(); }},
        {"POST", [](HttpResponse* self) { self->HandlePost(); }},
        {"OPTIONS", [](HttpResponse* self) { self->HandleOptions(); }}};

    try
    {
        if (auto it = METHODS.find(m_request->method); it != METHODS.end())
        {
            it->second(this); // 显式传递this，避免悬垂引用
        }
        else
        {
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

void HttpResponse::SendFile(const std::string& path)
{
    m_mmf.Init(path);
    m_response.content = std::string(m_mmf.data(), m_mmf.size());
    AddHeader("Content-Type", GetMimeType(path));
    AddHeader("Content-Length", std::to_string(m_mmf.size()));
}

void HttpResponse::SendError(int code, const std::string& message)
{
    m_response.code = code;

    nlohmann::json error = {
        {"error", g_httpStatusCodeMap.at(code)},
        {"message", message}};

    SendJSON(error);
}

void HttpResponse::SendJSON(const nlohmann::json& data)
{
    std::string jsonStr = data.dump();
    m_response.content  = jsonStr;

    SetContentType("application/json");
    AddHeader("Content-Length", std::to_string(jsonStr.size()));
}

void HttpResponse::HandleGet()
{
    ParsePath();
    std::string filePath = g_resourcePath + m_response.path;
    if (!FileUtil::IsFileExist(filePath))
    {
        return SendError(404, "Not Found");
    }
    SendFile(filePath);
}

void HttpResponse::HandlePost()
{
    std::string contextType = m_request->headers["content-type"];
    if (contextType == "application/x-www-form-urlencoded")
    {
    }
    else if (contextType == "application/json")
    {
        nlohmann::json obj = nlohmann::json::parse(m_request->body);
        // to do
        SendJSON({{"status", "success"}});
    }
    else if (contextType == "multipart/form-data")
    {
        // to do
    }
    else
    {
        SendError(415, "Unsupported Media Type");
    }
}

void HttpResponse::HandleOptions()
{
    m_response.code = 204;
    AddHeader("Allow", "GET, POST, OPTIONS");
    AddHeader("Content-Length", "0");
}

void HttpResponse::ParsePath()
{
    // 安全校验
    if (m_request->path.find("..") != std::string::npos)
    {
        throw std::runtime_error("Path traversal detected");
    }

    if (m_request->path == "/")
    {
        m_response.path = "/index.html";
    }
    else
    {
        for (auto& item : g_localHTML_Map)
        {
            if (item == m_request->path)
            {
                m_response.path = m_request->path + ".html";
                break;
            }
        }
    }

    if (m_response.path.empty()) // 并不是请求HTML的
    {
        m_response.path = m_request->path;
    }
}

void HttpResponse::PrepareCommonHeaders()
{
    AddHeader("Server", "IOCP HttpServer/1.0");
    AddHeader("Date", GetCurrentHttpDate());

    if (m_request->keep_alive)
    {
        AddHeader("Connection", "keep-alive");
        AddHeader("Keep-Alive", "timeout=120, max=10");
    }
    else
    {
        AddHeader("Connection", "close");
    }
}

std::string HttpResponse::GetCurrentHttpDate() const
{
    char buf[64];
    time_t now = time(nullptr);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    return buf;
}

std::string HttpResponse::GetMimeType(const std::string& path) const
{
    // 判断文件类型
    auto ext = path.substr(path.find_last_of('.'));
    return g_contentTypeMap.count(ext) ? g_contentTypeMap.at(ext) : "text/plain";
}