#include "HttpResponse.h"
#include "FileUtil.h"
#include "MMapFile.h"
#include <unordered_map>
#include <unordered_set>

using namespace util;

void HttpResponse::Init(HttpRequestState* state)
{
    m_reqState = state;
}

std::string HttpResponse::GetResponseMessage()
{
    std::string message;
    ParsePath();
    //
    message += AddResponseLine();
    message += AddResponseHeader();
    message += AddResponseContent();
    return message;
}

int HttpResponse::Code() const
{
    return m_respState.respCode;
}

void HttpResponse::ParsePath()
{
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

    m_respState.respCode      = 200;
    std::string localFilePath = g_resourcePath + m_respState.respPath;
    if (!FileUtil::IsFileExist(localFilePath))
    {
        m_respState.respCode = 404;
    }
}

void HttpResponse::ParsePost()
{
}

std::string HttpResponse::AddResponseLine()
{
    std::string responseLine;
    responseLine = std::string("HTTP/1.1") + " " + std::to_string(m_respState.respCode) + " " + g_httpStatusCodeMap.find(m_respState.respCode)->second;
    responseLine += "\r\n";
    return responseLine;
}

std::string HttpResponse::AddResponseHeader()
{
    static const std::string keep  = "Connection: keep-alive\r\n";
    static const std::string close = "Connection: close\r\n";

    std::string result;
    if (m_reqState->reqIsKeepAlive)
    {
        result += keep;
        result += "keep-alive: max=6, timeout=120\r\n";
    }
    else
    {
        result += close;
    }
    result += "Content-type: " + GetFileType() + "\r\n";
    return result;
}

std::string HttpResponse::AddResponseContent()
{
    m_mmf.Init(g_resourcePath + m_respState.respPath);

    std::string header = "Content-length: " + std::to_string(m_mmf.size()) + "\r\n\r\n";

    std::string result;
    result += header;
    result += std::string(m_mmf.data(), m_mmf.size());
    return result;
}

std::string HttpResponse::GetFileType()
{
    // 判断文件类型
    std::string::size_type idx = m_respState.respPath.find_last_of('.');
    if (idx == std::string::npos)
    {
        return "text/plain";
    }

    std::string suffix = m_respState.respPath.substr(idx);
    if (g_contentTypeMap.count(suffix) == 1)
    {
        return g_contentTypeMap.find(suffix)->second;
    }
    return "text/plain";
}