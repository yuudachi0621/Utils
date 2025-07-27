#pragma once
#include "HttpDefine.h"
#include "MMapFile.h"
#include <string>

class HttpResponse
{
public:
    void Init(HttpRequestState* state);

    std::string GetResponseMessage();

    int Code() const;

private:
    void ParsePath();

    void ParsePost();

    // 生成 Response
    std::string AddResponseLine();
    std::string AddResponseHeader();
    std::string AddResponseContent();

    std::string GetFileType();

private:
    MMapFile m_mmf;                // 文件映射
    HttpRequestState* m_reqState;  // 请求信息
    HttpResponseState m_respState; // 响应信息
};