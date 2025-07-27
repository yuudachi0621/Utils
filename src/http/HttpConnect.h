#pragma once
#include "HttpDefine.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <string>

class HttpConnect
{
public:
public:
    HttpConnect()  = default;
    ~HttpConnect() = default;

    void ParseRequest(const std::string& message);

    std::string GenerateResponse();

    bool IsKeepAlive() const;

private:
    HttpRequestState m_state;
    HttpRequest m_request;
    HttpResponse m_response;
};