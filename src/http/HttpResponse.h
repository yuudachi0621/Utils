#pragma once
#include "HttpDefine.h"
#include "MMapFile.h"

#include "json.hpp"

#include <map>
#include <memory>
#include <string>

/**
 * 可被 IOCP 直接发送的响应包。
 *
 * 小响应使用 header + body 两个连续内存块；
 * 文件响应使用 header + mmap 文件区间，WSASend 通过 scatter/gather 直接发送，
 * 不再把视频内容复制到 std::string。
 */
struct HttpResponsePacket
{
    std::string header;             // 状态行、响应头和空行
    std::string body;               // 动态响应正文
    std::shared_ptr<MMapFile> file; // 文件响应时的映射对象
    size_t fileOffset = 0;
    size_t fileLength = 0;

    size_t BodySize() const
    {
        return file ? fileLength : body.size();
    }

    size_t TotalSize() const
    {
        return header.size() + BodySize();
    }

    bool Empty() const
    {
        return TotalSize() == 0;
    }

    static HttpResponsePacket FromString(std::string text)
    {
        HttpResponsePacket packet;
        packet.header = std::move(text);
        return packet;
    }
};

// 负责根据已解析的 HttpRequestState 生成响应头、响应正文和最终 HTTP 报文。
// 同时负责静态文件发送、单段 Range 处理和业务 Handler 分派。
class HttpResponse
{
public:
    // 绑定请求并重置响应状态；每个请求必须重新调用。
    void Init(HttpRequestState* request);

    // 将状态行、响应头和正文序列化为完整 HTTP 响应，兼容非 IOCP 调用方。
    std::string BuildResponse();

    // 生成零拷贝友好的响应包，文件正文由 IOCP 直接通过 mmap + WSASend 发送。
    HttpResponsePacket BuildResponsePacket();

    int GetStatusCode() const;

    // 请求处理方法
    void HandleRequest();

    // 头部操作
    void AddHeader(const std::string& key, const std::string& value);
    void SetContentType(const std::string& type);

    // 快速响应方法。
    // SendFile 自动处理单段 Range，支持视频进度拖拽、206 Partial Content 和 416。
    void SendFile(const std::string& path);
    void SendError(int code, const std::string& message);
    void SendJSON(const nlohmann::json& data);
    void SendText(const std::string& text, const std::string& type = "text/plain; charset=utf-8");

private:
    // HTTP 方法处理
    void HandleGet();
    void HandleHead();
    void HandlePost();
    void HandlePut();
    void HandlePatch();
    void HandleDelete();
    void HandleOptions();
    void HandleTrace();

    // 将 URL 路径安全映射到资源根目录，阻止目录穿越。
    bool BuildResourcePath(std::string& filePath) const;

    // PUT/PATCH 共用的文件写入逻辑；requireExisting 控制是否允许创建文件。
    bool WriteResource(const std::string& filePath, bool requireExisting);
    std::string GetRequestContentType() const;
    std::string GetRequestHeader(const std::string& key) const;

    void PrepareCommonHeaders();
    std::map<std::string, std::string> ParseBody();
    bool LoginVerify(const std::string& name, const std::string& pwd);
    bool RegisterUser(const std::string& name, const std::string& pwd);

private:
    std::string GetCurrentHttpDate() const;
    std::string GetMimeType(const std::string& path) const;

private:
    std::shared_ptr<MMapFile> m_mmf; // 文件类响应使用的映射对象
    size_t m_fileOffset = 0;         // 文件响应起始偏移
    size_t m_fileLength = 0;         // 文件响应发送长度

    HttpRequestState* m_request = nullptr; // 当前请求状态，不拥有该对象
    HttpResponseState m_response;          // 当前响应状态
};
