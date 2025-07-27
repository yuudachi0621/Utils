#include "HttpResponse.h"
#include "FileUtil.h"
#include "MMapFile.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace util;

void HttpResponse::Init(HttpRequestState* state)
{
    m_mmf.unmap();
    m_respState.reset();
    m_reqState = state;

    // 设置默认头部
    PrepareCommonHeaders();
}

std::string HttpResponse::BuildResponse()
{
    std::ostringstream oss;

    // 状态行
    oss << m_respState.respVersion << " "
        << m_respState.respCode << " "
        << g_httpStatusCodeMap.at(m_respState.respCode) << "\r\n";

    // 头部
    for (const auto& [key, value] : m_respState.respHeaders)
    {
        oss << key << ": " << value << "\r\n";
    }
    oss << "\r\n"; // 头部结束空行

    // 内容
    if (!m_respState.content.empty())
    {
        oss.write(m_respState.content.data(), m_respState.content.size());
    }
    return oss.str();
}

int HttpResponse::GetStatusCode() const
{
    return m_respState.respCode;
}

void HttpResponse::HandleRequest()
{
    // 改为线程局部静态变量（每个线程独立拷贝）
    static thread_local const std::unordered_map<std::string, std::function<void(HttpResponse*)>> METHODS = {
        {"GET", [](HttpResponse* self) { self->HandleGet(); }},
        {"POST", [](HttpResponse* self) { self->HandlePost(); }},
        {"OPTIONS", [](HttpResponse* self) { self->HandleOptions(); }}};
    try
    {
        if (auto it = METHODS.find(m_reqState->reqMethod); it != METHODS.end())
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
    m_respState.respHeaders[key] = value;
}

void HttpResponse::SetContentType(const std::string& type)
{
    AddHeader("Content-Type", type);
}

void HttpResponse::SendFile(const std::string& path)
{
    m_mmf.Init(path);
    m_respState.content = std::string(m_mmf.data(), m_mmf.size());
    AddHeader("Content-Type", GetMimeType(path));
    AddHeader("Content-Length", std::to_string(m_mmf.size()));
}

void HttpResponse::SendError(int code, const std::string& message)
{
    m_respState.respCode = code;

    nlohmann::json error = {
        {"error", g_httpStatusCodeMap.at(code)},
        {"message", message}};

    SendJSON(error);
}

void HttpResponse::SendJSON(const nlohmann::json& data)
{
    std::string jsonStr = data.dump();
    m_respState.content = jsonStr;

    SetContentType("application/json");
    AddHeader("Content-Length", std::to_string(jsonStr.size()));
}

void HttpResponse::HandleGet()
{
    ParsePath();
    std::string filePath = g_resourcePath + m_respState.respPath;
    if (!FileUtil::IsFileExist(filePath))
    {
        return SendError(404, "Not Found");
    }
    SendFile(filePath);
}

void HttpResponse::HandlePost()
{
    std::string contextType = m_reqState->reqHeaders["content-type"];
    if (contextType == "application/x-www-form-urlencoded")
    {
    }
    else if (contextType == "application/json")
    {
        nlohmann::json obj = nlohmann::json::parse(m_reqState->reqBody);
        // to do
        SendJSON({{"status", "success"}});
    }
    else if (contextType == "multipart/form-data")
    {
    }
    else
    {
        SendError(415, "Unsupported Media Type");
    }
}

void HttpResponse::HandleOptions()
{
    m_respState.respCode = 204;
    AddHeader("Allow", "GET, POST, OPTIONS");
    AddHeader("Content-Length", "0");
}

void HttpResponse::ParsePath()
{
    // 安全校验
    if (m_reqState->reqPath.find("..") != std::string::npos)
    {
        throw std::runtime_error("Path traversal detected");
    }

    if (m_reqState->reqPath == "/")
    {
        m_respState.respPath = "/index.html";
    }
    else
    {
        for (auto& item : g_localHTML_Map)
        {
            if (item == m_reqState->reqPath)
            {
                m_respState.respPath = m_reqState->reqPath + ".html";
                break;
            }
        }
    }

    if (m_respState.respPath.empty()) // 并不是请求HTML的
    {
        m_respState.respPath = m_reqState->reqPath;
    }
}

void HttpResponse::PrepareCommonHeaders()
{
    AddHeader("Server", "IOCP HttpServer/1.0");
    AddHeader("Date", GetCurrentHttpDate());

    if (m_reqState->reqIsKeepAlive)
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

std::string HttpResponse::GetMimeType(const std::string& path)
{
    // 判断文件类型
    auto ext = path.substr(path.find_last_of('.'));
    return g_contentTypeMap.count(ext) ? g_contentTypeMap.at(ext) : "application/octet-stream";
}