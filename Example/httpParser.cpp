#include "httpParser.h"
#include "stringUtil.h"
#include <regex>

using namespace util;

void httpParser::Init(const ClientInfo& client)
{
    m_reqMessage = std::string(client.readBuff.GetValidData(), client.readBuff.ValidLength());
}

bool httpParser::Parser(const std::string& message)
{
    m_reqMessage                    = message;
    std::vector<std::string> reqVec = StringUtil::Split(m_reqMessage, "\r\n");
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

bool httpParser::ParserQuestLine(const std::string& line)
{
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        m_reqMethod = subMatch[1];
        m_reqPath   = subMatch[2];
        m_version   = subMatch[3];
        return true;
    }
    return false;
}

void httpParser::ParserPath()
{
    if (m_reqPath == "/")
    {
        m_reqPath = "/index.html";
    }
}

bool httpParser::ParserQuestHeader(const std::vector<std::string>& headers)
{
    // 预编译正则（键名：非空且不含控制字符；值：允许前后空格）
    static const std::regex s_header_regex(R"(^([^:\s]+):[ \t]*(.*)$)");
    std::smatch subMatch;

    for (auto& header : headers)
    {
        if (!std::regex_match(header, subMatch, s_header_regex))
        {
            continue; // 或记录日志
        }

        std::string key = subMatch[1];
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        std::string value_view = subMatch[2]; // C++17
        size_t start           = value_view.find_first_not_of(" \t");
        size_t end             = value_view.find_last_not_of(" \t\r\n");
        std::string value      = (start == std::string::npos) ? "" : std::string(value_view.substr(start, end - start + 1));

        if (m_reqHeader.count(key))
        {
            m_reqHeader[key] += ", " + value;
        }
        else
        {
            m_reqHeader[key] = value;
        }
    }
    return true;
}

bool httpParser::ParserBody(const std::string& body)
{
    m_body = body;
    return true;
}
