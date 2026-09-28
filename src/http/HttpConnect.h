#pragma once
#include "HttpDefine.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <string>

/**
 * HTTP 请求生命周期协调器。
 *
 * 使用顺序：
 * 1. ParseRequest() 解析请求头；
 * 2. WebServer 根据 Content-Length/chunked 调用 SetRequestContent()；
 * 3. GenerateResponse() 调用 HttpRequest/HttpResponse 完成业务处理并构建响应文本。
 */
class HttpConnect
{
public:
    HttpConnect()  = default;
    ~HttpConnect() = default;

    // 解析 HTTP 请求头。请求体由 WebServer 单独设置，避免解析器耦合 socket 协议。
    void ParseRequest(const std::string& message);

    // 根据已解析的请求分派 Handler，并返回完整 HTTP 响应报文。
    std::string GenerateResponse();

    // IOCP 应优先使用该接口：文件响应会保留 mmap 区间，由 WSASend scatter/gather 直接发送。
    HttpResponsePacket GenerateResponsePacket();

    // 解析失败或协议错误时生成独立的最小响应，不依赖当前请求是否有效。
    std::string GenerateErrorResponse(int code, const std::string& message, bool keepAlive = false) const;

    // 请求状态查询接口。
    bool IsValid() const;
    bool IsKeepAlive() const;
    bool IsChunked() const;
    bool ExpectsContinue() const;

    // 解析 Content-Length。非法值返回 size_t 最大值，由 WebServer 转换为 400。
    size_t GetContentLength() const;

    // 设置请求体。右值版本用于避免大请求体额外复制。
    void SetRequestContent(const std::string& content);
    void SetRequestContent(std::string&& content);

private:
    HttpRequestState m_state; // 当前请求解析状态，随 HttpConnect 生命周期存在
    HttpRequest m_request;    // 请求解析器
    HttpResponse m_response;  // 当前响应构建器
};
