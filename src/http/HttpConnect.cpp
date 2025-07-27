#include "HttpConnect.h"
#include "log.h"

using namespace util;

void HttpConnect::ParseRequest(const std::string& message)
{
    m_state.reqComleteMessage = message;
    m_request.Init(&m_state);
    if (!m_request.Parser())
    {
        LOG_INFO("request parsing failed, message:%s", message.data());
    }
}

std::string HttpConnect::GenerateResponse()
{
    m_response.Init(&m_state);
    return m_response.GetResponseMessage();
}

bool HttpConnect::IsKeepAlive() const
{
    return m_state.reqIsKeepAlive;
}
