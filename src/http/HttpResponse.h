#pragma once
#include "HttpDefine.h"
#include "MMapFile.h"

#include "json.hpp"

#include <map>
#include <string>

class HttpResponse
{
public:
    void Init(HttpRequestState* request);
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
    std::map<std::string, std::string> ParseBody();
    bool LoginVerify(const std::string& name, const std::string& pwd);
    bool RegisterUser(const std::string& name, const std::string& pwd);

private:
    std::string GetCurrentHttpDate() const;
    std::string GetMimeType(const std::string& path) const;

private:
    MMapFile m_mmf;                        // 文件映射
    HttpRequestState* m_request = nullptr; // 请求信息
    HttpResponseState m_response;          // 响应信息
};