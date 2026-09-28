#include "HttpRequest.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace {
std::string_view Trim(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

std::string ToLower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool ContainsToken(std::string_view value, std::string_view token)
{
    std::string lower  = ToLower(value);
    std::string target = ToLower(token);
    return lower.find(target) != std::string::npos;
}
} // namespace

// 绑定状态对象并清空上一次请求，避免 Keep-Alive 场景复用旧字段。
void HttpRequest::Init(HttpRequestState* state)
{
    m_state = state;
    if (m_state)
        m_state->reset();
}

// 解析顺序：请求行 -> Header 逐行解析 -> 计算 Keep-Alive/chunked/Expect 状态。
bool HttpRequest::Parser()
{
    if (!m_state)
        return false;

    m_state->valid             = false;
    const std::string& message = m_state->comleteMessage;
    const size_t headerEnd     = message.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    const size_t requestLineEnd = message.find("\r\n");
    if (requestLineEnd == std::string::npos || requestLineEnd > headerEnd)
        return false;

    if (!ParserQuestLine(message.substr(0, requestLineEnd)))
        return false;

    // 请求行之后逐行解析请求头，重复字段按 HTTP 语义合并。
    size_t pos = requestLineEnd + 2;
    while (pos < headerEnd)
    {
        const size_t lineEnd = message.find("\r\n", pos);
        if (lineEnd == std::string::npos || lineEnd > headerEnd)
            return false;

        if (!ParserQuestHeader(message.substr(pos, lineEnd - pos)))
            return false;

        pos = lineEnd + 2;
    }

    m_state->chunked        = ContainsToken(GetHeader("transfer-encoding"), "chunked");
    m_state->expectContinue = ContainsToken(GetHeader("expect"), "100-continue");
    m_state->keep_alive     = IsKeepAlive();
    m_state->valid          = true;
    return true;
}

std::string HttpRequest::GetReqPath() const
{
    return m_state ? m_state->path : std::string();
}

std::string HttpRequest::GetBody() const
{
    return m_state ? m_state->body : std::string();
}

std::string HttpRequest::GetMethod() const
{
    return m_state ? m_state->method : std::string();
}

std::string HttpRequest::GetVersion() const
{
    return m_state ? m_state->version : std::string();
}

// Header 查询使用小写键；调用者传入的键必须与解析存储规则一致。
std::string HttpRequest::GetHeader(const std::string& key) const
{
    if (!m_state)
        return std::string();

    const auto it = m_state->headers.find(key);
    return it == m_state->headers.end() ? std::string() : it->second;
}

bool HttpRequest::IsValid() const
{
    return m_state && m_state->valid;
}

// HTTP/1.1 默认长连接，HTTP/1.0 默认短连接，显式 Connection 头优先。
bool HttpRequest::IsKeepAlive() const
{
    if (!m_state)
        return false;

    const auto it = m_state->headers.find("connection");
    if (it != m_state->headers.end())
    {
        if (ContainsToken(it->second, "close"))
            return false;
        if (ContainsToken(it->second, "keep-alive"))
            return true;
    }

    // HTTP/1.1 默认复用连接，HTTP/1.0 默认关闭连接。
    return m_state->version == "1.1";
}

bool HttpRequest::IsChunked() const
{
    return m_state && m_state->chunked;
}

bool HttpRequest::ExpectsContinue() const
{
    return m_state && m_state->expectContinue;
}

// 解析形如：GET /index?q=1 HTTP/1.1 的请求行。
bool HttpRequest::ParserQuestLine(const std::string& line)
{
    const size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos)
        return false;

    const size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos)
        return false;

    const std::string method      = line.substr(0, firstSpace);
    const std::string target      = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const std::string httpVersion = line.substr(secondSpace + 1);

    if (method.empty() || target.empty() || httpVersion.rfind("HTTP/", 0) != 0)
        return false;

    m_state->method  = method;
    m_state->version = httpVersion.substr(5);

    // 只接受 HTTP/1.x，避免把 HTTP/2 的二进制帧误当成文本请求。
    if (m_state->version != "1.0" && m_state->version != "1.1")
        return false;

    // 查询字符串不参与静态资源路径匹配，单独保存给业务层使用。
    const size_t queryPos = target.find('?');
    m_state->path         = target.substr(0, queryPos);
    m_state->query        = (queryPos == std::string::npos) ? std::string() : target.substr(queryPos + 1);

    // fragment 不应当发送给服务器；遇到时将它与 path 分离。
    const size_t fragmentPos = m_state->path.find('#');
    if (fragmentPos != std::string::npos)
        m_state->path.resize(fragmentPos);

    return !m_state->path.empty();
}

// 解析单行 Header；键转小写，值去除首尾空白，同名 Header 合并。
bool HttpRequest::ParserQuestHeader(const std::string& line)
{
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0)
        return false;

    const std::string_view rawKey   = Trim(std::string_view(line).substr(0, colon));
    const std::string_view rawValue = Trim(std::string_view(line).substr(colon + 1));
    if (rawKey.empty())
        return false;

    for (unsigned char c : rawKey)
    {
        if (std::isspace(c) || std::iscntrl(c) || c == ':')
            return false;
    }

    const std::string key = ToLower(rawKey);
    std::string value(rawValue);
    const auto it = m_state->headers.find(key);
    // 同名 header 合并为逗号分隔值，方便 Connection/TE/Expect 统一判断。
    if (it == m_state->headers.end())
        m_state->headers.emplace(key, std::move(value));
    else
        it->second += ", " + value;

    return true;
}
