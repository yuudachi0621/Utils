#include "WebServer_IOCP.h"
#include "HttpConnect.h"
#include "log.h"
#include "sqlConnPool.h"

#include <MSWSock.h>

#include <algorithm>
#include <limits>
#include <string_view>

using namespace util;

namespace {
constexpr size_t kInvalidSize = std::numeric_limits<size_t>::max();

std::string_view TrimView(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

bool IsHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

enum class ChunkedParseResult
{
    Complete,
    Incomplete,
    Error,
};

// 解析 HTTP/1.1 chunked 请求体；consumed 包含最后一个 chunk 的终止 CRLF。
ChunkedParseResult ParseChunkedBody(std::string_view input,
                                    size_t maxBodySize,
                                    size_t& consumed,
                                    std::string& body)
{
    consumed = 0;
    body.clear();
    size_t pos = 0;

    while (true)
    {
        const size_t lineEnd = input.find("\r\n", pos);
        if (lineEnd == std::string_view::npos)
            return ChunkedParseResult::Incomplete;

        std::string_view sizeLine = TrimView(input.substr(pos, lineEnd - pos));
        const size_t extensionPos = sizeLine.find(';');
        if (extensionPos != std::string_view::npos)
            sizeLine = TrimView(sizeLine.substr(0, extensionPos));
        if (sizeLine.empty())
            return ChunkedParseResult::Error;

        size_t chunkSize = 0;
        for (const char c : sizeLine)
        {
            if (!IsHex(c))
                return ChunkedParseResult::Error;
            if (chunkSize > (kInvalidSize - static_cast<size_t>(HexValue(c))) / 16)
                return ChunkedParseResult::Error;
            chunkSize = chunkSize * 16 + static_cast<size_t>(HexValue(c));
        }

        pos = lineEnd + 2;
        if (chunkSize == 0)
        {
            // 无 trailer 时，最后一个 chunk 后直接是 CRLF；有 trailer 时以空行结束。
            if (input.size() >= pos + 2 && input.substr(pos, 2) == "\r\n")
            {
                consumed = pos + 2;
                return ChunkedParseResult::Complete;
            }

            const size_t trailersEnd = input.find("\r\n\r\n", pos);
            if (trailersEnd == std::string_view::npos)
                return ChunkedParseResult::Incomplete;

            consumed = trailersEnd + 4;
            return ChunkedParseResult::Complete;
        }

        if (chunkSize > maxBodySize || body.size() > maxBodySize - chunkSize)
            return ChunkedParseResult::Error;

        if (input.size() - pos < chunkSize + 2)
            return ChunkedParseResult::Incomplete;

        body.append(input.data() + pos, chunkSize);
        pos += chunkSize;
        if (input.substr(pos, 2) != "\r\n")
            return ChunkedParseResult::Error;
        pos += 2;
    }
}

bool IsExpectedSocketCloseError(DWORD error)
{
    switch (error)
    {
    case ERROR_NETNAME_DELETED:
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAESHUTDOWN:
    case ERROR_OPERATION_ABORTED:
        return true;
    default:
        return false;
    }
}

std::string BuildContinueResponse()
{
    return "HTTP/1.1 100 Continue\r\n\r\n";
}

// 将响应包剩余部分拆成最多两个 WSABUF：header 和 body/file。
// 这样可以避免把 mmap 文件区间复制到连续内存，直接交给 WSASend scatter/gather 发送。
void FillWriteBuffers(const HttpResponsePacket& packet,
                      size_t offset,
                      std::array<WSABUF, 2>& buffers,
                      ULONG& bufferCount)
{
    bufferCount = 0;

    const size_t totalSize = packet.TotalSize();
    if (offset >= totalSize)
        return;

    const size_t maxBufferLength = static_cast<size_t>((std::numeric_limits<ULONG>::max)());

    // 优先发送还未发完的 HTTP Header。
    if (offset < packet.header.size())
    {
        const size_t length      = (std::min)(maxBufferLength, packet.header.size() - offset);
        buffers[bufferCount].buf = const_cast<char*>(packet.header.data() + offset);
        buffers[bufferCount].len = static_cast<ULONG>(length);
        ++bufferCount;
        offset += length;

        // Header 本身仍未发完时，本轮只能发送一个 WSABUF。
        if (offset < packet.header.size())
            return;
    }

    if (offset >= totalSize)
        return;

    const size_t bodyOffset = offset - packet.header.size();
    const size_t length     = (std::min)(maxBufferLength, totalSize - offset);

    if (packet.file)
    {
        buffers[bufferCount].buf = const_cast<char*>(packet.file->data() + packet.fileOffset + bodyOffset);
    }
    else
    {
        buffers[bufferCount].buf = const_cast<char*>(packet.body.data() + bodyOffset);
    }

    buffers[bufferCount].len = static_cast<ULONG>(length);
    ++bufferCount;
}
} // namespace

// 初始化顺序：线程池 -> 数据库连接池 -> 日志 -> 监听 socket -> IOCP 工作线程。
WebServer_IOCP::WebServer_IOCP(int port,
                               bool OptLinger,
                               int sqlPort,
                               const char* sqlUser,
                               const char* sqlPwd,
                               const char* dbName,
                               int connPoolNum,
                               int threadNum,
                               bool openLog,
                               int logLevel)
    : m_port(port), m_openLiger(OptLinger), m_isRunning(false)
{
    if (threadNum <= 0)
        threadNum = static_cast<int>(std::max(2u, std::thread::hardware_concurrency()));

    m_threadPool = std::make_shared<util::ThreadPool>(static_cast<size_t>(threadNum));
    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (openLog)
        Log::GetInstance().Init(logLevel, "./log", ".log", true);

    if (!InitSocket())
    {
        LOG_ERROR("IOCP Server initialization failed");
        throw std::runtime_error("IOCP Server initialization failed");
    }

    // 工作线程会在启动时检查该标志，因此必须先置位再创建线程。
    m_isRunning.store(true);
    if (!InitIOCP())
    {
        m_isRunning.store(false);
        LOG_ERROR("IOCP Server initialization failed");
        throw std::runtime_error("IOCP Server initialization failed");
    }
    LOG_INFO("========== Server init (IOCP) ==========");
    LOG_INFO("Port:%d, OpenLinger: %s", m_port, m_openLiger ? "true" : "false");
    LOG_INFO("Log Level: %d", logLevel);
    LOG_INFO("resourcePath: %s", g_resourcePath.data());
    LOG_INFO("Connect database: %s", dbName);
    LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
}

// 析构顺序：停止接受新连接 -> 取消未完成 I/O -> 唤醒并等待工作线程 -> 关闭句柄。
WebServer_IOCP::~WebServer_IOCP()
{
    m_isRunning.store(false);

    // Cancel pending AcceptEx and connection I/O before waking worker threads.
    {
        std::shared_lock<std::shared_mutex> lock(m_connectionsMutex);
        for (const auto& [raw, context] : m_activeConnections)
        {
            (void)raw;
            if (!context)
                continue;

            if (context->accepting.load())
                CancelIoEx(reinterpret_cast<HANDLE>(m_listenFd), &context->acceptIo.overlapped);
            if (context->socket != INVALID_SOCKET)
                CancelIoEx(reinterpret_cast<HANDLE>(context->socket), nullptr);
        }
    }
    CancelIoEx(reinterpret_cast<HANDLE>(m_listenFd), nullptr);

    for (size_t i = 0; i < m_workerThreads.size(); ++i)
        PostQueuedCompletionStatus(m_iocpHandle, 0, kShutdownKey, nullptr);

    for (auto& worker : m_workerThreads)
    {
        if (worker.joinable())
            worker.join();
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_connectionsMutex);
        for (auto& [raw, context] : m_activeConnections)
        {
            (void)raw;
            if (context)
                context->CloseSocketNoThrow();
        }
        m_activeConnections.clear();
    }

    if (m_iocpHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_iocpHandle);
        m_iocpHandle = INVALID_HANDLE_VALUE;
    }

    if (m_listenFd != INVALID_SOCKET)
    {
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
    }
}

// 启动时一次性预投递 AcceptEx，之后由完成回调持续补投。
void WebServer_IOCP::Start()
{
    LOG_INFO("========== Server start (IOCP) ==========");

    for (size_t i = 0; i < kPrePostAccept; ++i)
        PostAccept();

    while (m_isRunning.load())
        Sleep(1000);
}

// 创建监听 socket，设置 Linger/端口复用，并绑定监听地址。
bool WebServer_IOCP::InitSocket()
{
    if (m_port <= 0 || m_port > 65535)
        return false;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<u_short>(m_port));

    linger optLinger{};
    if (m_openLiger)
    {
        optLinger.l_onoff  = 1;
        optLinger.l_linger = 1;
    }

    m_listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenFd == INVALID_SOCKET)
    {
        LOG_ERROR("create listen socket failed: %d", WSAGetLastError());
        return false;
    }

    if (setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&optLinger), sizeof(optLinger)) == SOCKET_ERROR)
    {
        LOG_ERROR("set SO_LINGER failed: %d", WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    const int reuseAddress = 1;
    if (setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress)) == SOCKET_ERROR)
    {
        LOG_ERROR("set SO_REUSEADDR failed: %d", WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        LOG_ERROR("bind port %d failed: %d", m_port, WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    if (listen(m_listenFd, SOMAXCONN) == SOCKET_ERROR)
    {
        LOG_ERROR("listen port %d failed: %d", m_port, WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    return true;
}

// 创建完成端口并关联监听 socket，然后启动 IOCP 工作线程。
bool WebServer_IOCP::InitIOCP()
{
    m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (m_iocpHandle == nullptr)
    {
        LOG_ERROR("CreateIoCompletionPort failed: %d", GetLastError());
        return false;
    }

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_listenFd), m_iocpHandle, kAcceptKey, 0) == nullptr)
    {
        LOG_ERROR("associate listen socket failed: %d", GetLastError());
        CloseHandle(m_iocpHandle);
        m_iocpHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    const unsigned int hardwareThreads = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned int i = 0; i < hardwareThreads; ++i)
        m_workerThreads.emplace_back(&WebServer_IOCP::WorkerThread, this);

    return true;
}

// 每个工作线程批量取出完成包；业务处理不在 IOCP 线程中阻塞执行。
void WebServer_IOCP::WorkerThread()
{
    OVERLAPPED_ENTRY entries[kCompletionBatch]{};

    // 工作线程只在收到显式停止包后退出，确保析构时取消的 I/O 完成包能被处理。
    while (true)
    {
        ULONG completionCount = 0;
        const BOOL result     = GetQueuedCompletionStatusEx(
            m_iocpHandle,
            entries,
            static_cast<ULONG>(kCompletionBatch),
            &completionCount,
            INFINITE,
            FALSE);

        if (!result)
        {
            const DWORD error = GetLastError();
            if (!m_isRunning.load() || error == ERROR_ABANDONED_WAIT_0)
                break;
            continue;
        }

        bool shouldStop = false;
        for (ULONG i = 0; i < completionCount; ++i)
        {
            OVERLAPPED_ENTRY& entry = entries[i];
            if (entry.lpCompletionKey == kShutdownKey)
            {
                shouldStop = true;
                continue;
            }

            HandleCompletion(entry.dwNumberOfBytesTransferred, entry.lpOverlapped);
        }

        if (shouldStop)
            break;
    }
}

void WebServer_IOCP::HandleCompletion(DWORD bytesTransferred, LPOVERLAPPED overlapped)
{
    if (!overlapped)
        return;

    IoOverlapped* operation                    = CONTAINING_RECORD(overlapped, IoOverlapped, overlapped);
    std::shared_ptr<ConnectionContext> context = FindContextByRaw(operation->owner);
    if (!context)
        return;

    // 每个 AcceptEx 完成后都补投一个新的 AcceptEx，维持预投递连接池容量。
    if (operation->operation == IoOperation::Accept && m_isRunning.load())
        PostAccept();

    // GetQueuedCompletionStatusEx 不会在 OVERLAPPED_ENTRY 中直接给出每个操作的错误码，
    // 因此需要再查询 OVERLAPPED，准确区分成功、取消和连接断开。
    DWORD transferred     = bytesTransferred;
    const HANDLE ioHandle = operation->operation == IoOperation::Accept ? reinterpret_cast<HANDLE>(m_listenFd) : reinterpret_cast<HANDLE>(context->socket);
    const BOOL completed  = GetOverlappedResult(ioHandle, overlapped, &transferred, FALSE);

    if (!completed)
    {
        const DWORD error = GetLastError();
        if (!IsExpectedSocketCloseError(error))
            LOG_ERROR("I/O operation failed: %d, socket: %d", error, context->socket);

        CloseConnection(context);
        FinishIo(context);
        return;
    }

    switch (operation->operation)
    {
    case IoOperation::Accept:
        HandleAccept(context, transferred, TRUE);
        break;
    case IoOperation::Read:
        HandleRead(context, transferred, TRUE);
        break;
    case IoOperation::Write:
        HandleWrite(context, transferred, TRUE);
        break;
    }

    FinishIo(context);
}

void WebServer_IOCP::HandleAccept(const std::shared_ptr<ConnectionContext>& context,
                                  DWORD bytesTransferred,
                                  BOOL success)
{
    context->accepting.store(false);

    if (!success || !context || context->socket == INVALID_SOCKET || context->closed.load())
    {
        CloseConnection(context);
        return;
    }

    sockaddr* localAddress  = nullptr;
    sockaddr* remoteAddress = nullptr;
    int localAddressLength  = 0;
    int remoteAddressLength = 0;

    GetAcceptExSockaddrs(
        context->acceptBuffer.data(),
        bytesTransferred,
        sizeof(sockaddr_in) + 16,
        sizeof(sockaddr_in) + 16,
        &localAddress,
        &localAddressLength,
        &remoteAddress,
        &remoteAddressLength);

    // AcceptEx 完成后必须更新 socket 上下文，后续 I/O 才能继承监听 socket 的地址和选项。
    SOCKET listenSocket = m_listenFd;
    if (setsockopt(context->socket,
                   SOL_SOCKET,
                   SO_UPDATE_ACCEPT_CONTEXT,
                   reinterpret_cast<const char*>(&listenSocket),
                   sizeof(listenSocket))
        == SOCKET_ERROR)
    {
        LOG_ERROR("SO_UPDATE_ACCEPT_CONTEXT failed: %d", WSAGetLastError());
        CloseConnection(context);
        return;
    }

    const int sendBufferSize    = 128 * 1024;
    const int receiveBufferSize = 128 * 1024;
    setsockopt(context->socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBufferSize), sizeof(sendBufferSize));
    setsockopt(context->socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBufferSize), sizeof(receiveBufferSize));

    const int noDelay = 1;
    setsockopt(context->socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(context->socket), m_iocpHandle, kConnKey, 0) == nullptr)
    {
        LOG_ERROR("associate client socket failed: %d", GetLastError());
        CloseConnection(context);
        return;
    }

    (void)localAddress;
    (void)remoteAddress;
    (void)localAddressLength;
    (void)remoteAddressLength;

    LOG_DEBUG("New connection: socket %d", context->socket);
    PostRead(context);
}

void WebServer_IOCP::HandleRead(const std::shared_ptr<ConnectionContext>& context,
                                DWORD bytesTransferred,
                                BOOL success)
{
    context->readInProgress.store(false);

    if (!success || bytesTransferred == 0 || !context || context->closed.load())
    {
        CloseConnection(context);
        return;
    }

    // 读完成后立即释放进行中标记；解析任务结束前不会再次发起读，避免缓冲区并发访问。
    context->readBuff.AddWritePos(bytesTransferred);
    if (context->readBuff.ValidLength() > kMaxHeaderBytes + kMaxBodyBytes + 4096)
    {
        CloseConnection(context);
        return;
    }

    ProcessClientData(context);
}

void WebServer_IOCP::HandleWrite(const std::shared_ptr<ConnectionContext>& context,
                                 DWORD bytesTransferred,
                                 BOOL success)
{
    if (!success || !context || context->closed.load())
    {
        CloseConnection(context);
        return;
    }

    // WSASend 可能只发送一部分数据；保留 currentWrite 和偏移量并继续发送剩余部分。
    bool shouldClose = false;
    {
        std::lock_guard<std::mutex> lock(context->writeMutex);
        context->writeInProgress.store(false);

        const size_t totalSize = context->currentWrite.TotalSize();
        if (bytesTransferred == 0 || context->currentWriteOffset + bytesTransferred > totalSize)
        {
            shouldClose = true;
        }
        else
        {
            context->currentWriteOffset += bytesTransferred;
            if (context->currentWriteOffset >= totalSize)
            {
                // 当前响应包已发送完，释放 packet；文件映射也会随 shared_ptr 自动释放。
                context->currentWrite       = HttpResponsePacket{};
                context->currentWriteOffset = 0;
                shouldClose                 = context->sendQueue.empty() && context->closeAfterWrite.load();
            }
        }
    }

    if (shouldClose)
    {
        CloseConnection(context);
        return;
    }

    PostWrite(context);
}

void WebServer_IOCP::ProcessClientData(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context || context->closed.load() || context->processing.exchange(true))
        return;

    try
    {
        // 每条连接同一时刻只允许一个解析任务，避免 readBuff 被多个线程消费。
        m_threadPool->enqueue([this, context]() {
            ProcessRequests(context);
            context->processing.store(false);

            if (!context->closed.load() && !context->closeAfterWrite.load())
                PostRead(context);
        });
    } catch (const std::exception& e)
    {
        context->processing.store(false);
        LOG_ERROR("enqueue request task failed: %s", e.what());
        CloseConnection(context);
    }
}

// 从 readBuff 中循环提取完整请求，支持粘包、拆包和 HTTP pipeline。
void WebServer_IOCP::ProcessRequests(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context || context->closed.load())
        return;

    auto enqueueError = [&](int code, const std::string& message) {
        // 必须在投递写操作前设置关闭标志，避免写完成事件先于标志设置而漏关连接。
        context->closeAfterWrite.store(true);
        HttpConnect error;
        const std::string response = error.GenerateErrorResponse(code, message, false);
        EnqueueResponse(context, HttpResponsePacket::FromString(response));
    };

    // 同一读缓冲区可能包含多个请求，循环解析以支持粘包和 HTTP pipeline。
    while (!context->closed.load())
    {
        const size_t available = context->readBuff.ValidLength();
        const char* data       = context->readBuff.GetValidData();
        const std::string_view view(data, available);

        const size_t headerEnd = view.find("\r\n\r\n");
        if (headerEnd == std::string_view::npos)
        {
            if (available > kMaxHeaderBytes)
                enqueueError(431, "Request Header Fields Too Large");
            break;
        }

        const size_t headerLength = headerEnd + 4;
        if (headerLength > kMaxHeaderBytes)
        {
            enqueueError(431, "Request Header Fields Too Large");
            break;
        }

        HttpConnect connection;
        connection.ParseRequest(std::string(data, headerLength));
        if (!connection.IsValid())
        {
            enqueueError(400, "Bad Request");
            break;
        }

        const bool chunked         = connection.IsChunked();
        const size_t contentLength = connection.GetContentLength();
        if (chunked && contentLength != 0)
        {
            enqueueError(400, "Content-Length and chunked body cannot be combined");
            break;
        }

        size_t totalLength = headerLength;
        if (chunked)
        {
            size_t consumed = 0;
            std::string body;
            const ChunkedParseResult result = ParseChunkedBody(
                view.substr(headerLength),
                kMaxBodyBytes,
                consumed,
                body);

            if (result == ChunkedParseResult::Incomplete)
            {
                if (connection.ExpectsContinue())
                {
                    const std::string response = BuildContinueResponse();
                    EnqueueResponse(context, HttpResponsePacket::FromString(response));
                }
                break;
            }
            if (result == ChunkedParseResult::Error)
            {
                enqueueError(400, "Invalid chunked body");
                break;
            }

            connection.SetRequestContent(std::move(body));
            totalLength += consumed;
        }
        else
        {
            if (contentLength == kInvalidSize)
            {
                enqueueError(400, "Invalid Content-Length");
                break;
            }
            if (contentLength > kMaxBodyBytes)
            {
                enqueueError(413, "Payload Too Large");
                break;
            }
            if (available < headerLength + contentLength)
            {
                if (connection.ExpectsContinue())
                {
                    const std::string response = BuildContinueResponse();
                    EnqueueResponse(context, HttpResponsePacket::FromString(response));
                }
                break;
            }

            if (contentLength != 0)
                connection.SetRequestContent(std::string(data + headerLength, contentLength));
            totalLength += contentLength;
        }

        context->readBuff.Consume(totalLength);

        const bool keepAlive = connection.IsKeepAlive();
        if (!keepAlive)
            context->closeAfterWrite.store(true);

        HttpResponsePacket response = connection.GenerateResponsePacket();
        EnqueueResponse(context, std::move(response));

        if (!keepAlive)
            break;
    }
}

// 创建一个新的 AcceptEx 上下文并投递异步接受请求。
void WebServer_IOCP::PostAccept()
{
    auto context    = ConnectionContext::Create();
    context->socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (context->socket == INVALID_SOCKET)
    {
        LOG_ERROR("WSASocket failed: %d", WSAGetLastError());
        return;
    }

    context->acceptIo.owner     = context.get();
    context->acceptIo.operation = IoOperation::Accept;
    ZeroMemory(&context->acceptIo.overlapped, sizeof(OVERLAPPED));
    context->accepting.store(true);
    context->pendingIo.fetch_add(1);
    RegisterConnection(context);

    if (context->closed.load())
    {
        FinishIo(context);
        return;
    }

    DWORD bytes         = 0;
    const BOOL accepted = AcceptEx(
        m_listenFd,
        context->socket,
        context->acceptBuffer.data(),
        0,
        sizeof(sockaddr_in) + 16,
        sizeof(sockaddr_in) + 16,
        &bytes,
        &context->acceptIo.overlapped);

    if (!accepted && WSAGetLastError() != ERROR_IO_PENDING)
    {
        LOG_ERROR("AcceptEx failed: %d", WSAGetLastError());
        context->accepting.store(false);
        CloseConnection(context);
        FinishIo(context);
    }
}

void WebServer_IOCP::PostRead(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed.load())
        return;

    bool expected = false;
    // CAS 保证每条连接最多只有一个未完成的 WSARecv。
    if (!context->readInProgress.compare_exchange_strong(expected, true))
        return;

    context->pendingIo.fetch_add(1);
    if (context->closed.load())
    {
        context->readInProgress.store(false);
        FinishIo(context);
        return;
    }

    context->readIo.owner     = context.get();
    context->readIo.operation = IoOperation::Read;
    ZeroMemory(&context->readIo.overlapped, sizeof(OVERLAPPED));

    context->readWsaBuf.buf = context->readBuff.WritableBegin();
    context->readWsaBuf.len = static_cast<ULONG>(context->readBuff.WritableLength());

    DWORD flags     = 0;
    DWORD bytesRead = 0;
    if (WSARecv(context->socket,
                &context->readWsaBuf,
                1,
                &bytesRead,
                &flags,
                &context->readIo.overlapped,
                nullptr)
        == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            context->readInProgress.store(false);
            LOG_DEBUG("WSARecv failed: %d, socket: %d", error, context->socket);
            CloseConnection(context);
            FinishIo(context);
        }
    }
}

void WebServer_IOCP::PostWrite(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context || context->closed.load())
        return;

    bool shouldClose = false;
    {
        std::lock_guard<std::mutex> lock(context->writeMutex);
        if (context->closed.load() || context->writeInProgress.load())
            return;

        // currentWrite 代表当前响应包；完成回调前不能替换，文件 mmap 由 packet 持有。
        if (context->currentWriteOffset >= context->currentWrite.TotalSize())
        {
            context->currentWrite       = HttpResponsePacket{};
            context->currentWriteOffset = 0;
            if (context->sendQueue.empty())
                return;

            context->currentWrite = std::move(context->sendQueue.front());
            context->sendQueue.pop_front();
        }

        if (context->currentWrite.Empty())
            return;

        context->pendingIo.fetch_add(1);
        if (context->closed.load())
        {
            shouldClose = true;
        }
        else
        {
            context->writeInProgress.store(true);
            context->writeIo.owner     = context.get();
            context->writeIo.operation = IoOperation::Write;
            ZeroMemory(&context->writeIo.overlapped, sizeof(OVERLAPPED));

            FillWriteBuffers(context->currentWrite,
                             context->currentWriteOffset,
                             context->writeWsaBufs,
                             context->writeBufferCount);

            if (context->writeBufferCount == 0)
            {
                context->writeInProgress.store(false);
                return;
            }

            DWORD bytesSent = 0;
            DWORD flags     = 0;
            if (WSASend(context->socket,
                        context->writeWsaBufs.data(),
                        context->writeBufferCount,
                        &bytesSent,
                        flags,
                        &context->writeIo.overlapped,
                        nullptr)
                == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error != WSA_IO_PENDING)
                {
                    context->writeInProgress.store(false);
                    shouldClose = true;
                }
            }
        }
    }

    if (shouldClose)
    {
        CloseConnection(context);
        FinishIo(context);
    }
}

void WebServer_IOCP::EnqueueResponse(const std::shared_ptr<ConnectionContext>& context, HttpResponsePacket&& response)
{
    if (!context || context->closed.load() || response.Empty())
        return;

    {
        std::lock_guard<std::mutex> lock(context->writeMutex);
        if (context->closed.load())
            return;
        context->sendQueue.emplace_back(std::move(response));
    }

    PostWrite(context);
}

// 标记逻辑关闭、取消 I/O，并在 pendingIo 归零后释放 socket。
void WebServer_IOCP::CloseConnection(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context)
        return;

    if (context->closed.exchange(true))
        return;

    context->closeAfterWrite.store(true);

    {
        std::lock_guard<std::mutex> lock(context->writeMutex);
        // currentWrite 可能正被未完成的 WSASend 引用，必须在完成包返回后再释放。
        context->sendQueue.clear();
    }

    if (context->accepting.load())
        CancelIoEx(reinterpret_cast<HANDLE>(m_listenFd), &context->acceptIo.overlapped);

    if (context->socket != INVALID_SOCKET)
    {
        ::shutdown(context->socket, SD_BOTH);
        CancelIoEx(reinterpret_cast<HANDLE>(context->socket), nullptr);
    }

    if (context->pendingIo.load() == 0)
    {
        context->CloseSocketNoThrow();
        UnregisterConnection(context);
    }
}

// 每个完成包调用一次；负责延迟释放连接，避免异步回调访问已析构对象。
void WebServer_IOCP::FinishIo(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context)
        return;

    // 每个异步操作开始前加一，完成后减一；计数归零且连接已关闭时才释放 socket。
    const int remaining = context->pendingIo.fetch_sub(1) - 1;
    if (remaining <= 0 && context->closed.load())
    {
        context->CloseSocketNoThrow();
        UnregisterConnection(context);
    }
}

// 注册 active 连接，完成回调通过裸指针恢复 shared_ptr。
void WebServer_IOCP::RegisterConnection(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context)
        return;

    std::unique_lock<std::shared_mutex> lock(m_connectionsMutex);
    m_activeConnections[context.get()] = context;
}

// 注销连接；仅当指针和 shared_ptr 都匹配时移除，防止误删新连接。
void WebServer_IOCP::UnregisterConnection(const std::shared_ptr<ConnectionContext>& context)
{
    if (!context)
        return;

    std::unique_lock<std::shared_mutex> lock(m_connectionsMutex);
    const auto it = m_activeConnections.find(context.get());
    if (it != m_activeConnections.end() && it->second == context)
        m_activeConnections.erase(it);
}

std::shared_ptr<ConnectionContext> WebServer_IOCP::FindContextByRaw(ConnectionContext* raw)
{
    if (!raw)
        return nullptr;

    std::shared_lock<std::shared_mutex> lock(m_connectionsMutex);
    const auto it = m_activeConnections.find(raw);
    return it == m_activeConnections.end() ? nullptr : it->second;
}
