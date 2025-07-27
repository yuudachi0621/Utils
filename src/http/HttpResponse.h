#pragma once
#include "HttpDefine.h"
#include "MMapFile.h"

#include "json.hpp"

#include <string>

class HttpResponse
{
public:
    void Init(HttpRequestState* state);
    std::string BuildResponse();
    int GetStatusCode() const;

    // 请求处理方法
    void HandleRequest();

    // 头部操作
    void AddHeader(const std::string& key, const std::string& value);
    void SetContentType(const std::string& type);

    // 快速响应方法
    void SendFile(const std::string& path);
    void SendError(int code, const std::string& message);
    void SendJSON(const nlohmann::json& data);

private:
    // 处理请求
    void HandleGet();
    void HandlePost();
    void HandleOptions();
    void ParsePath();

    void PrepareCommonHeaders();
    std::string GetCurrentHttpDate() const;
    std::string GetMimeType(const std::string& path);

private:
    MMapFile m_mmf;                // 文件映射
    HttpRequestState* m_reqState;  // 请求信息
    HttpResponseState m_respState; // 响应信息
};