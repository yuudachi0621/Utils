#pragma once
#include "HttpDefine.h"
#include <string>
#include <unordered_map>

class HttpRequest
{
public:
    HttpRequest()  = default;
    ~HttpRequest() = default;

    void Init(HttpRequestState* state);

    bool Parser();

    std::string GetReqPath() const;

    std::string GetBody() const;

    bool IsKeepAlive() const;

    std::string GetMethod() const;
    std::string GetVersion() const;

private:
    bool ParserQuestLine(const std::string& line);

    bool ParserQuestHeader(const std::vector<std::string>& headers);

    bool ParserBody(const std::string& body);

private:
    HttpRequestState* m_state;
};