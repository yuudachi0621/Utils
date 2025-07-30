#pragma once
#include "HttpDefine.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <string>

class HttpConnect
{
public:
    HttpConnect()  = default;
    ~HttpConnect() = default;

    void ParseRequest(const std::string& message);

    std::string GenerateResponse();

    size_t GetContentLength() const;

    void SetRequestContent(const std::string content);

    bool IsKeepAlive() const;

private:
    HttpRequestState m_state;
    HttpRequest m_request;
    HttpResponse m_response;
};