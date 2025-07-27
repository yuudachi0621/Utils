#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>

// 资源路径
static const std::string g_resourcePath = "";

struct HttpRequestState
{
    std::string reqComleteMessage;                           // 请求完整报文
    std::string reqMethod;                                   // 请求方法
    std::string reqPath;                                     // 请求的资源URL
    std::string reqVersion;                                  // HTTP版本
    bool reqIsKeepAlive;                                     // Keep-Alive
    std::unordered_map<std::string, std::string> reqHeaders; // 请求头
    std::string reqBody;                                     // 请求体

    void reset()
    {
        reqComleteMessage.clear();
        reqMethod.clear();
        reqPath.clear();
        reqVersion.clear();
        reqIsKeepAlive = false;
        reqHeaders.clear();
        reqBody.clear();
    }
};

struct HttpResponseState
{
    std::string respVersion; // HTTP版本
    int respCode;            // 状态码
    std::string respPath;    // 响应资源路径
    std::string respContent; // 响应体
};

const std::unordered_set<std::string> g_localHTML_Map = {
    "/index",
    "/register",
    "/login",
    "/welcome",
    "/video",
    "/picture",
};

const std::unordered_map<int, std::string> g_httpStatusCodeMap = {
    {200, "OK"},                         //	请求成功。一般用于GET与POST请求
    {400, "Bad Request"},                // 客户端请求的语法错误，服务器无法理解
    {403, "Forbidden"},                  // 服务器理解请求客户端的请求，但是拒绝执行此请求
    {404, "Not Found"},                  // 服务器无法根据客户端的请求找到资源（网页）
    {500, "Internal Server Error"},      // 服务器内部错误，无法完成请求
    {501, "Not Implemented"},            // 服务器不支持请求的功能，无法完成请求
    {505, "HTTP Version not supported"}, // 服务器不支持请求的HTTP协议的版本，无法完成处理
};

const std::unordered_map<std::string, std::string> g_contentTypeMap = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/nsword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css "},
    {".js", "text/javascript "},
};