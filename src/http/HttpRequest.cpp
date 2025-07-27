#include "HttpRequest.h"
#include "log.h"
#include <regex>

using namespace util;

void HttpRequest::Init(HttpRequestState* state)
{
    m_state = state;
}

bool HttpRequest::Parser()
{
    std::vector<std::string> reqVec = StringUtil::Split(m_state->comleteMessage, "\r\n");
    if (reqVec.size() < 3)
        return false;

    if (!ParserQuestLine(reqVec[0]))
        return false;

    if (!ParserQuestHeader(std::vector<std::string>(reqVec.begin() + 1, reqVec.end() - 1)))
        return false;

    if (!ParserBody(reqVec[reqVec.size() - 2]))
        return false;

    return true;
}

std::string HttpRequest::GetReqPath() const
{
    return m_state->path;
}

std::string HttpRequest::GetBody() const
{
    return m_state->body;
}

bool HttpRequest::IsKeepAlive() const
{
    if (m_state->headers.count("connection") == 1)
    {
        if (m_state->headers.find("connection")->second == "keep-alive")
        {
            m_state->keep_alive = true;
            return true;
        }
        m_state->keep_alive = false;
        return false;
    }
}
std::string HttpRequest::GetMethod() const
{
    return m_state->method;
}

std::string HttpRequest::GetVersion() const
{
    return m_state->version;
}

bool HttpRequest::ParserQuestLine(const std::string& line)
{
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        m_state->method  = subMatch[1];
        m_state->path    = subMatch[2];
        m_state->version = subMatch[3];
        return true;
    }
    LOG_ERROR("Parse Request Line Error");
    return false;
}

bool HttpRequest::ParserQuestHeader(const std::vector<std::string>& headers)
{
    // 预编译正则（键名：非空且不含控制字符；值：允许前后空格）
    static const std::regex s_header_regex(R"(^([^:\s]+):[ \t]*(.*)$)");
    std::smatch subMatch;

    for (auto& header : headers)
    {
        if (header.empty()) continue;

        if (!std::regex_match(header, subMatch, s_header_regex))
        {
            LOG_ERROR("Parse Request Header Error, error header:%s", header.data());
            continue;
        }
        // key转小写
        std::string key = subMatch[1];
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        std::string value_view = subMatch[2];
        size_t start           = value_view.find_first_not_of(" \t");
        size_t end             = value_view.find_last_not_of(" \t\r\n");
        std::string value      = (start == std::string::npos) ? "" : std::string(value_view.substr(start, end - start + 1));

        if (m_state->headers.count(key))
        {
            m_state->headers[key] += ", " + value;
        }
        else
        {
            m_state->headers[key] = value;
        }
    }

    // 记录Keep-Alive
    IsKeepAlive();
    return true;
}

bool HttpRequest::ParserBody(const std::string& body)
{
    m_state->body = body;
    return true;
}
