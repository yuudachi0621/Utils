#include "WebServer_IOCP.h"
#include "HttpConnect.h"
#include "log.h"
#include "sqlConnPool.h"
#include <algorithm>

#ifdef _MSC_VER
#include <MSWSock.h>
#endif

using namespace util;

WebServer_IOCP::WebServer_IOCP(int port, bool OptLinger, int sqlPort, const char* sqlUser, const char* sqlPwd, const char* dbName, int connPoolNum, int threadNum, bool openLog, int logLevel)
    : m_port(port), m_openLiger(OptLinger), m_isRunning(false)
{
    if (threadNum <= 0) threadNum = std::thread::hardware_concurrency();
    m_threadPool = std::make_shared<util::ThreadPool>(threadNum);

    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (openLog) Log::GetInstance().Init(logLevel, "./log", ".log", true);

    if (!InitSocket() || !InitIOCP())
    {
        LOG_ERROR("IOCP Server initialization failed");
        LOG_ERROR("========== Server init IOCP error!==========");

        throw std::runtime_error("IOCP Server initialization failed");
    }
    else
    {
        m_isRunning = true;
        LOG_INFO("========== Server init (IOCP) ==========");
        LOG_INFO("Port:%d, OpenLinger: %s", m_port, m_openLiger ? "true" : "false");
        LOG_INFO("Log Level: %d", logLevel);
        LOG_INFO("resourcePath: %s", g_resourcePath.data());
        LOG_INFO("Connect database: %s", dbName);
        LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
    }
}

WebServer_IOCP::~WebServer_IOCP()
{
    m_isRunning = false;

    // 停止工作线程
    for (int i = 0; i < static_cast<int>(m_workerThreads.size()); ++i)
    {
        PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);
    }

    // 关闭所有活跃连接
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        for (auto& kv : m_activeConnections)
        {
            if (kv.second)
            {
                kv.second->SafeClose();
            }
        }
        m_activeConnections.clear();
    }

    for (auto& t : m_workerThreads)
    {
        if (t.joinable()) t.join();
    }
    if (m_iocpHandle != INVALID_HANDLE_VALUE) CloseHandle(m_iocpHandle);
    if (m_listenFd != INVALID_SOCKET) closesocket(m_listenFd);
}

void WebServer_IOCP::Start()
{
    LOG_INFO("========== Server start (IOCP) ==========");

    // 预投递多个AcceptEx
    int preAcceptCount = 200;
    for (int i = 0; i < preAcceptCount; ++i)
    {
        PostAccept();
    }
    while (m_isRunning)
    {
        // 死循环
        Sleep(1000);
    }
}

bool WebServer_IOCP::InitSocket()
{
    int ret;
    if (m_port > 65535 || m_port < 1024)
    {
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(m_port);

    struct linger optLinger = {0};
    if (m_openLiger)
    {
        optLinger.l_onoff  = 1;
        optLinger.l_linger = 1;
    }

    // 创建 socket
    m_listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenFd == INVALID_SOCKET)
    {
        LOG_ERROR("port: %d ,create socket error: %d", m_port, WSAGetLastError());
        return false;
    }

    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, (const char*)&optLinger, sizeof(optLinger));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("init linger error: %d", WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(int));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("set SO_REUSEADDR error: %d", WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    // 绑定 socket
    ret = bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("bind port: %d error: %d", m_port, WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    // 监听连接
    ret = listen(m_listenFd, SOMAXCONN);
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("listen port: %d error: %d", m_port, WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }

    // 设置非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(m_listenFd, FIONBIO, &mode) == SOCKET_ERROR)
    {
        LOG_ERROR("ioctlsocket error: %d", WSAGetLastError());
        closesocket(m_listenFd);
        m_listenFd = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool WebServer_IOCP::InitIOCP()
{
    // 创建IOCP句柄
    m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!m_iocpHandle)
    {
        LOG_ERROR("CreateIoCompletionPort failed: %d", GetLastError());
        return false;
    }

    // 关联监听socket到IOCP
    if (!CreateIoCompletionPort((HANDLE)m_listenFd, m_iocpHandle, kAcceptKey, 0))
    {
        LOG_ERROR("Associate listen socket failed: %d", GetLastError());
        CloseHandle(m_iocpHandle);
        m_iocpHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // 创建工作线程
    unsigned int threadCount = std::thread::hardware_concurrency() * 2;
    for (unsigned int i = 0; i < threadCount; ++i)
    {
        m_workerThreads.emplace_back(&WebServer_IOCP::WorkerThread, this);
    }
    return true;
}

void WebServer_IOCP::WorkerThread()
{
    while (m_isRunning)
    {
        DWORD bytesTransferred  = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        // 处理关闭信号
        if (completionKey == 0 && bytesTransferred == 0 && overlapped == nullptr)
            break;

        if (!result)
        {
            DWORD error = GetLastError();
            if (overlapped)
            {
                if (completionKey == kConnKey)
                {
                    ConnectionContext* ctx = CONTAINING_RECORD(overlapped, ConnectionContext, readOv);
                    auto context           = FindContextByRaw(ctx);
                    if (!context) // 可能是写操作
                    {
                        ctx     = CONTAINING_RECORD(overlapped, ConnectionContext, writeOv);
                        context = FindContextByRaw(ctx);
                    }
                    if (context)
                    {
                        // 常见的连接断开错误
                        if (error == ERROR_NETNAME_DELETED || error == WSAECONNRESET || error == WSAECONNABORTED || error == WSAESHUTDOWN)
                        {
                            LOG_DEBUG("Connection reset by peer, socket: %d", context->socket);
                        }
                        else
                        {
                            LOG_ERROR("I/O operation failed: %d, socket: %d", error, context->socket);
                        }
                        CloseConnection(context);
                    }
                }
                else if (completionKey == kAcceptKey)
                {
                    ConnectionContext* ctx = CONTAINING_RECORD(overlapped, ConnectionContext, readOv);
                    auto context           = FindContextByRaw(ctx);
                    if (context)
                    {
                        LOG_ERROR("AcceptEx failed: %d, socket: %d", error, context->socket);
                        CloseConnection(context);
                    }
                    // 重新投递Accept
                    PostAccept();
                }
            }
            continue;
        }

        if (completionKey == kAcceptKey) // ----- AcceptEx事件 -----
        {
            ConnectionContext* ctx                     = CONTAINING_RECORD(overlapped, ConnectionContext, readOv);
            std::shared_ptr<ConnectionContext> context = FindContextByRaw(ctx);
            if (context)
            {
                HandleNewConnection(context);
                LOG_DEBUG("New connection: socket %d", context->socket);
            }
            // 继续投递新的Accept
            PostAccept();
        }
        else if (completionKey == kConnKey) // ----- 普通IO事件 -----
        {
            ConnectionContext* ctx                     = CONTAINING_RECORD(overlapped, ConnectionContext, readOv);
            std::shared_ptr<ConnectionContext> context = FindContextByRaw(ctx);
            if (!context) // 可能是写操作完成
            {
                ConnectionContext* ctx = CONTAINING_RECORD(overlapped, ConnectionContext, writeOv);
                context                = FindContextByRaw(ctx);
                if (!context) continue;
            }

            if (overlapped == &context->readOv) // 读操作完成
            {
                if (bytesTransferred == 0) // 客户端关闭连接
                {
                    LOG_INFO("Connection closed by peer, socket: %d", context->socket);
                    CloseConnection(context);
                    continue;
                }
                context->readBuff.AddWritePos(bytesTransferred);
                ProcessClientData(context);
            }
            else if (overlapped == &context->writeOv) // 写操作完成
            {
                // 检查是否还有待发送数据
                if (!context->sendQueue.empty())
                {
                    PostWrite(context);
                }
                // 写操作完成后继续读取
                if (!context->closed && context->socket != INVALID_SOCKET)
                {
                    PostRead(context);
                }
            }
        }
    }
}

void WebServer_IOCP::ProcessClientData(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed) return;

    m_threadPool->enqueue([this, self = context->shared_from_this()]() mutable {
        while (true)
        {
            const char* data = self->readBuff.GetValidData();
            size_t len       = self->readBuff.ValidLength();

            if (len < 4)
                break;

            auto pos = util::StringUtil::Find(data, len, "\r\n\r\n", 4);
            if (pos == nullptr) // 未读到完整请求
                break;

            // HTTP请求头
            size_t headerLen = (pos - data) + 4;
            std::string httpHeader(data, headerLen);

            // 解析HTTP请求头
            HttpConnect hconn;
            hconn.ParseRequest(httpHeader);
            // 获取 Content-Length
            size_t contentLength = hconn.GetContentLength();
            // 请求总长度
            size_t totalRequestLen = headerLen + contentLength;
            if (self->readBuff.ValidLength() < totalRequestLen) // 数据不足，等待更多数据
            {
                break;
            }
            // 读取 Content
            std::string httpContent(data + headerLen, contentLength);
            if (!httpContent.empty())
                hconn.SetRequestContent(httpContent);

            // 此请求已读完
            self->readBuff.Consume(totalRequestLen);

            // 生成 Response 报文
            std::string httpResponse = hconn.GenerateResponse();
            // std::string httpResponse = "HTTP/1.1 200 OK\r\n"          // 状态行
            //                            "Content-Type: text/plain\r\n" // 响应头
            //                            "Content-Length: 13\r\n"       // 内容长度头
            //                            "Connection: keep-alive\r\n"   // 保持连接
            //                            "\r\n"                         // 空行分隔头部和正文
            //                            "Hello, World!";               // 响应正文

            // 放入响应发送队列
            self->sendQueue.push(std::vector<char>(httpResponse.begin(), httpResponse.end()));
        }
        // 发送响应
        PostWrite(self);
        //  写操作完成后继续读取
        if (!self->closed && self->socket != INVALID_SOCKET)
        {
            PostRead(self);
        }
    });
}

void WebServer_IOCP::PostAccept()
{
    auto context    = ConnectionContext::Create();
    context->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (context->socket == INVALID_SOCKET)
    {
        LOG_ERROR("WSASocket failed: %d", WSAGetLastError());
        return;
    }
    // 添加到连接表
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_activeConnections[context.get()] = context;
    }

    DWORD bytes = 0;
    ZeroMemory(&context->readOv, sizeof(OVERLAPPED)); // accept只用readOv
    if (!AcceptEx(m_listenFd,
                  context->socket,
                  context->acceptBuffer,
                  0,
                  sizeof(sockaddr_in) + 16,
                  sizeof(sockaddr_in) + 16,
                  &bytes,
                  &context->readOv)) // 使用readOv作为overlapped
    {
        if (WSAGetLastError() != ERROR_IO_PENDING)
        {
            LOG_ERROR("AcceptEx failed: %d", GetLastError());
            CloseConnection(context);
        }
    }
}

void WebServer_IOCP::HandleNewConnection(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET)
    {
        LOG_ERROR("HandleNewConnection: invalid context or socket");
        CloseConnection(context);
        return;
    }

    // 设置socket选项
    int buf = 8 * 1024; // 8KB
    setsockopt(context->socket, SOL_SOCKET, SO_RCVBUF, (char*)&buf, sizeof(buf));
    setsockopt(context->socket, SOL_SOCKET, SO_SNDBUF, (char*)&buf, sizeof(buf));

    int nodelay = 1;
    setsockopt(context->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    SOCKET listenSocket = m_listenFd; // 必须强制为SOCKET类型
    setsockopt(context->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof(listenSocket));

    // 关联到IOCP
    if (!CreateIoCompletionPort((HANDLE)context->socket, m_iocpHandle, kConnKey, 0))
    {
        LOG_ERROR("Associate socket to IOCP failed: %d", GetLastError());
        CloseConnection(context);
        return;
    }
    // 投递第一个读请求
    PostRead(context);
}

void WebServer_IOCP::PostRead(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed)
        return;

    context->wsaBuf.buf = context->readBuff.WritableBegin();
    context->wsaBuf.len = static_cast<ULONG>(context->readBuff.WritableLength());

    DWORD flags     = 0;
    DWORD bytesRead = 0;
    ZeroMemory(&context->readOv, sizeof(OVERLAPPED));
    if (WSARecv(context->socket, &context->wsaBuf, 1, &bytesRead, &flags, &context->readOv, nullptr) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSARecv failed: %d, socket: %d", error, context->socket);
            CloseConnection(context);
        }
    }
}

void WebServer_IOCP::PostWrite(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed)
        return;

    if (context->sendQueue.empty())
        return;

    auto data           = context->sendQueue.pop();
    context->wsaBuf.buf = data.data();
    context->wsaBuf.len = static_cast<ULONG>(data.size());

    DWORD flags = 0;
    ZeroMemory(&context->writeOv, sizeof(OVERLAPPED));
    if (WSASend(context->socket, &context->wsaBuf, 1, nullptr, flags, &context->writeOv, nullptr) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend failed: %d, socket: %d", error, context->socket);
            CloseConnection(context);
        }
    }
}

void WebServer_IOCP::CloseConnection(std::shared_ptr<ConnectionContext> context)
{
    if (!context) return;
    bool already = context->closed.exchange(true);
    if (already) return; // 避免二次关闭

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_activeConnections.erase(context.get());
    }
}

std::shared_ptr<ConnectionContext> WebServer_IOCP::FindContextByRaw(ConnectionContext* raw)
{
    if (!raw)
        return nullptr;

    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    auto it = m_activeConnections.find(raw);
    if (it != m_activeConnections.end())
    {
        return it->second;
    }
    return nullptr;
}