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
    m_port       = port;
    m_openLiger  = OptLinger;
    m_threadPool = std::make_unique<util::ThreadPool>(threadNum);
    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (openLog)
    {
        Log::GetInstance().Init(logLevel, "./log", ".log", true);
    }

    if (!InitSocket() || !InitIOCP(threadNum))
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
    CloseHandle(m_iocpHandle);
    closesocket(m_listenFd);
}

void WebServer_IOCP::Start()
{
    LOG_INFO("========== Server start (IOCP) ==========");
    PostAccept(); // 开始接收连接
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
    ret = listen(m_listenFd, 6);
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

bool WebServer_IOCP::InitIOCP(int threadNum)
{
    // 创建IOCP句柄
    m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!m_iocpHandle)
    {
        LOG_ERROR("CreateIoCompletionPort failed: %d", GetLastError());
        return false;
    }

    // 关联监听socket到IOCP
    if (!CreateIoCompletionPort((HANDLE)m_listenFd, m_iocpHandle, (ULONG_PTR)this, 0))
    {
        LOG_ERROR("Associate listen socket failed: %d", GetLastError());
        return false;
    }

    // 创建工作线程
    unsigned int threadCount = std::min(std::thread::hardware_concurrency() * 2, static_cast<unsigned int>(threadNum));
    for (unsigned int i = 0; i < threadCount; ++i)
    {
        m_threadPool->enqueue(&WebServer_IOCP::WorkerThread, this);
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

        // 获取完成事件
        BOOL result = GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred, &completionKey, &overlapped, INFINITE);
        if (!overlapped)
        {
            LOG_ERROR("Null overlapped in completion event");
            continue;
        }

        // 通过 overlapped 从成员地址反推结构体地址
        ConnectionContext* rawContext = CONTAINING_RECORD(overlapped, ConnectionContext, overlapped);
        std::shared_ptr<ConnectionContext> context;
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            auto it = std::find_if(m_activeConnections.begin(),
                                   m_activeConnections.end(),
                                   [rawContext](const auto& ptr) { return ptr.get() == rawContext; });
            if (it != m_activeConnections.end())
            {
                context = *it;
            }
            else
            {
                LOG_ERROR("ConnectionContext not found!");
                continue;
            }
        }

        if (!result)
        {
            DWORD error = GetLastError();
            if (overlapped != nullptr)
            {
                // 连接出错
                LOG_ERROR("IOCP operation failed: %d", error);
                CloseConnection(context);
            }
            continue;
        }

        if ((completionKey == (ULONG_PTR)this) && bytesTransferred == 0)
        {
            // 新连接到达
            HandleNewConnection(context);
            LOG_INFO("New connection: socket %d", context->socket);
        }
        else
        {
            // 已建立连接的操作完成
            if (context->isReadPending)
            {
                if (bytesTransferred == 0)
                {
                    CloseConnection(context); // 对端关闭连接
                    continue;
                }
                context->readBuff.AddWritePos(bytesTransferred);
                ProcessClientData(context);

                // 如果请求未完整（如未找到 \r\n\r\n），继续读取
                if (context->isReadPending)
                {
                    PostRead(context);
                }
            }
            else
            {
                // 写入完成
                context->writeBuff.AddReadPos(bytesTransferred);
                if (context->writeBuff.ValidLength() > 0)
                {
                    PostWrite(context); // 还有数据要写
                }
                else
                {
                    // 响应发送完毕，重新开始读取（支持 Keep-Alive）
                    context->writeBuff.Reset();
                    context->isReadPending = true;
                    PostRead(context);
                }
            }
        }
    }
}

void WebServer_IOCP::ProcessClientData(std::shared_ptr<ConnectionContext> context)
{
    // 处理完整请求
    if (util::StringUtil::Find(context->readBuff.GetValidData(), context->readBuff.ValidLength(), "\r\n\r\n", strlen("\r\n\r\n")) != nullptr)
    {
        ProcessHttpRequest(context);
        context->readBuff.Reset();
        context->isReadPending = false;
        PostWrite(context);
    }
}

void WebServer_IOCP::ProcessHttpRequest(std::shared_ptr<ConnectionContext> context)
{
    HttpConnect hconn;
    hconn.ParseRequest(context->readBuff.GetValidDataToStr());
    std::string httpResponse = hconn.GenerateResponse();
    context->writeBuff.Append(httpResponse.data(), httpResponse.size());
}

void WebServer_IOCP::PostAccept()
{
    // 创建新连接上下文
    auto context = ConnectionContext::Create();
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_activeConnections.insert(context); // 防止提前释放
    }

    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));
    context->socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    // 使用AcceptEx接收连接
    char acceptBuffer[sizeof(sockaddr_in) * 2 + 32] = {0}; // 用于接收地址信息
    if (!AcceptEx(m_listenFd,
                  context->socket,
                  acceptBuffer,
                  0,
                  sizeof(sockaddr_in) + 16,
                  sizeof(sockaddr_in) + 16,
                  NULL,
                  &context->overlapped))
    {
        if (WSAGetLastError() != ERROR_IO_PENDING)
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            m_activeConnections.erase(context);
            LOG_ERROR("AcceptEx failed: %d", GetLastError());
        }
    }
}

void WebServer_IOCP::HandleNewConnection(std::shared_ptr<ConnectionContext> context)
{
    // 设置新socket选项
    setsockopt(context->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&m_listenFd, sizeof(m_listenFd));

    // 关联到IOCP
    CreateIoCompletionPort((HANDLE)context->socket, m_iocpHandle, (ULONG_PTR)context.get(), 0);

    // 投递读请求
    context->isReadPending = true;
    PostRead(context);

    // 继续投递新的AcceptEx
    PostAccept();
}

void WebServer_IOCP::PostRead(std::shared_ptr<ConnectionContext> context)
{
    if (!context || context->socket == INVALID_SOCKET)
        return;

    // 重置 overlapped
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    DWORD flags         = 0;
    context->wsaBuf.buf = context->readBuff.WritableBegin();
    context->wsaBuf.len = context->readBuff.WritableLength();
    if (WSARecv(context->socket, &context->wsaBuf, 1, nullptr, &flags, &context->overlapped, nullptr) == SOCKET_ERROR)
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
    if (!context || context->socket == INVALID_SOCKET
        || context->writeBuff.ValidLength() == 0)
    {
        return;
    }

    // 重置 overlapped
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    DWORD flags         = 0;
    context->wsaBuf.buf = (char*)context->writeBuff.GetValidData();
    context->wsaBuf.len = context->writeBuff.ValidLength();
    if (WSASend(context->socket, &context->wsaBuf, 1, nullptr, flags, &context->overlapped, nullptr) == SOCKET_ERROR)
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

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        if (context->socket != INVALID_SOCKET)
        {
            LOG_INFO("Connection closed, socket: %d", context->socket);
            closesocket(context->socket);
            context->socket = INVALID_SOCKET;
        }
        m_activeConnections.erase(context); // 移除引用
    }
}