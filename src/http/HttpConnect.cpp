#include "HttpConnect.h"
#include "log.h"

#include <charconv>
#include <limits>

using namespace util;

// 第一阶段：只解析请求头和请求行，正文由 WebServer 按长度规则补充。
void HttpConnect::ParseRequest(const std::string& message)
{
    // Init 会清空上一次请求的状态，必须在它之后写入原始报文。
    m_request.Init(&m_state);
    m_state.comleteMessage = message;
    if (!m_request.Parser())
    {
        LOG_INFO("request parsing failed");
    }
}

// 第二阶段：初始化响应对象，按 HTTP 方法执行 Handler，最后构建完整报文。
std::string HttpConnect::GenerateResponse()
{
    m_response.Init(&m_state);
    m_response.HandleRequest();
    return m_response.BuildResponse();
}

// 生成零拷贝响应包，供 IOCP 网络层直接分块发送。
HttpResponsePacket HttpConnect::GenerateResponsePacket()
{
    m_response.Init(&m_state);
    m_response.HandleRequest();
    return m_response.BuildResponsePacket();
}

// 协议错误响应使用独立状态，确保解析失败时仍能返回合法 HTTP 报文。
std::string HttpConnect::GenerateErrorResponse(int code, const std::string& message, bool keepAlive) const
{
    HttpRequestState state;
    state.version    = "HTTP/1.1";
    state.keep_alive = keepAlive;
    state.valid      = true;

    HttpResponse response;
    response.Init(&state);
    response.SendError(code, message);
    return response.BuildResponse();
}

bool HttpConnect::IsValid() const
{
    return m_request.IsValid();
}

bool HttpConnect::IsKeepAlive() const
{
    return m_request.IsKeepAlive();
}

bool HttpConnect::IsChunked() const
{
    return m_request.IsChunked();
}

bool HttpConnect::ExpectsContinue() const
{
    return m_request.ExpectsContinue();
}

// Content-Length 必须是纯十进制数字；非法值使用 size_t 最大值作为哨兵。
size_t HttpConnect::GetContentLength() const
{
    const std::string value = m_request.GetHeader("content-length");
    if (value.empty())
        return 0;

    size_t result    = 0;
    const auto begin = value.data();
    const auto end   = begin + value.size();
    // 无效长度返回 max，由 WebServer 统一转换为 400 响应。
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc() || parsed.ptr != end)
        return (std::numeric_limits<size_t>::max)();

    return result;
}

void HttpConnect::SetRequestContent(const std::string& content)
{
    m_state.body = content;
}

void HttpConnect::SetRequestContent(std::string&& content)
{
    m_state.body = std::move(content);
}
