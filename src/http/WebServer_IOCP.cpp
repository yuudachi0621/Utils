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
{
    m_port      = port;
    m_openLiger = OptLinger;

    if (threadNum <= 0)
        threadNum = std::thread::hardware_concurrency(); // 建议设置合理的默认值

    m_threadPool = std::make_shared<util::ThreadPool>(threadNum);
    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (openLog)
    {
        Log::GetInstance().Init(logLevel, "./log", ".log", true);
    }

    if (!InitSocket() || !InitIOCP())
    {
        LOG_ERROR("IOCP Server initialization failed");
        LOG_ERROR("========== Server init IOCP error!==========");
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
    for (auto& t : m_workerThreads)
    {
        if (t.joinable()) t.join();
    }
    CloseHandle(m_iocpHandle);
    closesocket(m_listenFd);
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
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd == INVALID_SOCKET)
    {
        LOG_ERROR("port: %d ,create socket error!", m_port);
        return false;
    }

    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, (const char*)&optLinger, sizeof(optLinger));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("init linger error!");
        closesocket(m_listenFd);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(int));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("set socket setsockopt error !");
        closesocket(m_listenFd);
        return false;
    }

    // 绑定 socket
    ret = bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("bind port: %d error!", m_port);
        closesocket(m_listenFd);
        return false;
    }

    // 监听连接
    ret = listen(m_listenFd, 512);
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR("listen port: %d error!", m_port);
        closesocket(m_listenFd);
        return false;
    }

    // 设置非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(m_listenFd, FIONBIO, &mode) == SOCKET_ERROR)
    {
        closesocket(m_listenFd);
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
        return false;
    }

    // 创建工作线程
    unsigned int threadCount = std::thread::hardware_concurrency();
    m_workerThreads.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
    {
        m_workerThreads.emplace_back(&WebServer_IOCP::WorkerThread, this);
    }
    return true;
}

std::shared_ptr<ConnectionContext> WebServer_IOCP::FindContextByRaw(ConnectionContext* raw)
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    auto it = std::find_if(m_activeConnections.begin(),
                           m_activeConnections.end(),
                           [raw](const auto& p) { return p.get() == raw; });
    if (it != m_activeConnections.end())
        return *it;
    return nullptr;
}

void WebServer_IOCP::WorkerThread()
{
    while (m_isRunning)
    {
        DWORD bytesTransferred  = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        // 获取完成事件
        BOOL result = GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred, &completionKey, &overlapped, INFINITE);
        if (!overlapped) continue;

        // 判断所有Overlapped是哪类（read还是write）操作
        ConnectionContext* ctx                     = nullptr;
        bool isRead                                = false;
        ctx                                        = CONTAINING_RECORD(overlapped, ConnectionContext, readOv);
        std::shared_ptr<ConnectionContext> context = FindContextByRaw(ctx);
        if (context)
        {
            isRead = true;
        }
        else
        {
            // 不是readOv就一定是writeOv（你的套接字只有2个overlapped）
            ctx     = CONTAINING_RECORD(overlapped, ConnectionContext, writeOv);
            context = FindContextByRaw(ctx);
            if (!context) continue; // 无法识别context直接忽略
            isRead = false;
        }

        if (!result)
        {
            DWORD err = GetLastError();
            LOG_ERROR("GQCS fail! Key:%d err:%d ctx:%p", (int)completionKey, int(err), ctx);
            CloseConnection(context);
            continue;
        }

        if (completionKey == kAcceptKey) // 新连接到达
        {
            HandleNewConnection(context);
            LOG_INFO("New connection: socket %d", context->socket);
            continue;
        }
        else if (completionKey == kConnKey) // 已建立连接的操作完成
        {
            if (isRead) // 读
            {
                if (bytesTransferred == 0)
                {
                    CloseConnection(context); // 对端关闭连接
                    continue;
                }
                context->readBuff.AddWritePos(bytesTransferred);
                ProcessClientData(context);

                // 若未完整请求可继续投递
                if (!context->closed && context->socket != INVALID_SOCKET)
                    PostRead(context);
            }
            else // 写
            {
                context->writeBuff.AddReadPos(bytesTransferred);

                if (context->writeBuff.ValidLength() == 0)
                {
                    // 响应发送完毕，重新开始读取（支持 Keep-Alive）
                    context->writeBuff.Reset();
                    PostRead(context);
                }
                else
                {
                    PostWrite(context); // 还有数据要写
                }
            }
        }
    }
}

void WebServer_IOCP::ProcessClientData(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET) return;

    while (context->readBuff.ValidLength() >= 4)
    {
        auto pos = util::StringUtil::Find(context->readBuff.GetValidData(),
                                          context->readBuff.ValidLength(),
                                          "\r\n\r\n",
                                          strlen("\r\n\r\n"));
        if (pos == nullptr) // 未读到完整请求
            return;

        size_t reqLen = (pos - context->readBuff.GetValidData()) + strlen("\r\n\r\n");
        ProcessHttpRequest(context, reqLen);
        context->readBuff.Consume(reqLen);
    }
}

void WebServer_IOCP::ProcessHttpRequest(std::shared_ptr<ConnectionContext> context, size_t reqLen)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed) return;
    std::string httpRequest(context->readBuff.GetValidData(), reqLen); // 只取本次数据

    // 使用线程池处理HTTP请求
    m_threadPool->enqueue([this, self = context->shared_from_this(), httpRequest]() {
        HttpConnect hconn;
        hconn.ParseRequest(httpRequest);
        std::string httpResponse = hconn.GenerateResponse();

        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            if (self->socket == INVALID_SOCKET || self->closed) return;
            self->writeBuff.Reset();
            self->writeBuff.Append(httpResponse.data(), httpResponse.size());
        }
        // 在锁外投递写
        PostWrite(self);
    });
}

void WebServer_IOCP::PostAccept()
{
    auto context = ConnectionContext::Create();

    ZeroMemory(&context->readOv, sizeof(OVERLAPPED)); // accept只用readOv
    context->socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (context->socket == INVALID_SOCKET)
    {
        LOG_ERROR("WSASocket failed: %d", WSAGetLastError());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_activeConnections.insert(context); // 只有再insert前，所有I/O才安全
    }

    // 使用AcceptEx接收连接
    if (!AcceptEx(m_listenFd,
                  context->socket,
                  context->acceptBuffer,
                  0,
                  sizeof(sockaddr_in) + 16,
                  sizeof(sockaddr_in) + 16,
                  NULL,
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
    static constexpr int MAX_CONN = 10000;
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        if (m_activeConnections.size() >= MAX_CONN)
        {
            CloseConnection(context);
            return;
        }
    }

    if (context->socket == INVALID_SOCKET)
    {
        LOG_ERROR("Invalid socket in new connection");
        CloseConnection(context);
        return;
    }

    // 设置新socket选项
    int optVal = 1;
    setsockopt(context->socket, SOL_SOCKET, SO_RCVBUF, (char*)&optVal, sizeof(optVal));
    setsockopt(context->socket, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, sizeof(optVal));
    setsockopt(context->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&optVal, sizeof(optVal));
    setsockopt(context->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&m_listenFd, sizeof(m_listenFd));

    // 关联到IOCP
    CreateIoCompletionPort((HANDLE)context->socket, m_iocpHandle, kConnKey, 0);

    // 投递读请求
    PostRead(context);

    // 继续投递新的AcceptEx
    PostAccept();
}

void WebServer_IOCP::PostRead(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed)
        return;

    context->wsaBuf.buf = context->readBuff.WritableBegin();
    context->wsaBuf.len = context->readBuff.WritableLength();

    DWORD flags = 0;
    ZeroMemory(&context->readOv, sizeof(OVERLAPPED)); // 重置 overlapped
    if (WSARecv(context->socket, &context->wsaBuf, 1, nullptr, &flags, &context->readOv, nullptr) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSARecv failed: %d", error);
            CloseConnection(context);
        }
    }
}

void WebServer_IOCP::PostWrite(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET || context->closed)
        return;

    if (context->writeBuff.ValidLength() == 0)
    {
        PostRead(context);
        return;
    }

    size_t sendSize     = std::min(context->writeBuff.ValidLength(), static_cast<size_t>(64 * 1024));
    context->wsaBuf.buf = (char*)context->writeBuff.GetValidData();
    context->wsaBuf.len = static_cast<u_long>(sendSize);

    DWORD flags = 0;
    ZeroMemory(&context->writeOv, sizeof(OVERLAPPED)); // 重置 overlapped
    if (WSASend(context->socket, &context->wsaBuf, 1, nullptr, flags, &context->writeOv, nullptr) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend failed: %d", error);
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
        m_activeConnections.erase(context);
    }
    context->SafeClose();
}