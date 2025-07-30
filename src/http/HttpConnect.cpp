#include "HttpConnect.h"
#include "log.h"

using namespace util;

void HttpConnect::ParseRequest(const std::string& message)
{
    m_state.comleteMessage = message;
    m_request.Init(&m_state);
    if (!m_request.Parser())
    {
        LOG_INFO("request parsing failed, message:%s", message.data());
    }
}

std::string HttpConnect::GenerateResponse()
{
    m_response.Init(&m_state);
    m_response.HandleRequest();
    return m_response.BuildResponse();
}

size_t HttpConnect::GetContentLength() const
{
    auto it = m_state.headers.find("");
    if (it == m_state.headers.end())
        return 0;

    try
    {
        return std::stoull(it->second);
    } catch (const std::exception& e)
    {
        LOG_ERROR("request content-length parsing failed, message:%s", e.what());
        return 0;
    }
}

void HttpConnect::SetRequestContent(const std::string content)
{
    m_state.body = content;
}

bool HttpConnect::IsKeepAlive() const
{
    return m_state.keep_alive;
}
