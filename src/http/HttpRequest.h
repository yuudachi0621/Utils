#pragma once
#include "HttpDefine.h"
#include <string>

/**
 * 负责解析 HTTP/1.x 请求行和请求头。
 *
 * 设计边界：
 * - 只处理字符串解析，不负责 socket 读取；
 * - 请求体由 WebServer 根据 Content-Length 或 chunked 规则单独注入；
 * - 解析结果写入外部 HttpRequestState，对象本身不拥有请求状态。
 */
class HttpRequest
{
public:
    HttpRequest()  = default;
    ~HttpRequest() = default;

    // 绑定并重置请求状态；每次解析新请求前必须先调用。
    void Init(HttpRequestState* state);

    // 解析请求行和全部 Header，要求输入包含 \r\n\r\n 结束标记。
    bool Parser();

    // 以下访问器保留给外部调用方；内部解析通过 HttpRequestState 直接访问。
    std::string GetReqPath() const;
    std::string GetBody() const;
    std::string GetMethod() const;
    std::string GetVersion() const;

    // 返回指定小写 Header 的值；未找到时返回空字符串。
    std::string GetHeader(const std::string& key) const;

    // 协议状态查询。
    bool IsValid() const;
    bool IsKeepAlive() const;
    bool IsChunked() const;
    bool ExpectsContinue() const;

private:
    // 解析 HTTP 请求行的 method、target、version。
    bool ParserQuestLine(const std::string& line);

    // 解析单行 Header；同名 Header 会合并为逗号分隔值。
    bool ParserQuestHeader(const std::string& line);

private:
    HttpRequestState* m_state = nullptr;
};
