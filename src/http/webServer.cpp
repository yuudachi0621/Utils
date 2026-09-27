#include "webServer.h"
#include "HttpConnect.h"
#include "log.h"
#include "sqlConnPool.h"
#include "stringUtil.h"

using namespace util;

webServer::webServer(int port, bool OptLinger, int sqlPort, const char* sqlUser, const char* sqlPwd, const char* dbName, int connPoolNum, int threadNum, bool openLog, int logLevel)
{
    m_port       = port;
    m_openLiger  = OptLinger;
    m_threadPool = std::make_unique<util::ThreadPool>(threadNum);
    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (openLog)
    {
        Log::GetInstance().Init(logLevel, "./log", ".log", true);
    }

    if (!InitSocket())
    {
        LOG_ERROR("InitSocket failed");
        LOG_ERROR("========== Server init error!==========");
    }
    else
    {
        LOG_INFO("========== Server init ==========");
        LOG_INFO("Port:%d, OpenLinger: %s", m_port, m_openLiger ? "true" : "false");
        LOG_INFO("Log Level: %d", logLevel);
        LOG_INFO("resourcePath: %s", g_resourcePath.data());
        LOG_INFO("Connect database: %s", dbName);
        LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
    }
}

webServer::~webServer()
{
    closesocket(m_listenFd);
    sqlConnPool::Instance().ClosePool();
}

void webServer::Start()
{
    LOG_INFO("========== Server start ==========");

    fd_set readfds, writefds;
    struct timeval timeout;
    std::vector<ClientInfo> clients(webServerMaxCount);
    std::mutex clientsMutex; // 用于保护clients数组的线程安全

    // 初始化客户端数组
    for (auto& client : clients)
    {
        client.socket    = INVALID_SOCKET;
        client.needWrite = false;
        client.readBuff.Reset();
        client.writeBuff.Reset();
    }

    // 主循环
    while (true)
    {
        timeout.tv_sec  = 0; // 每次循环重置timeout, 0秒
        timeout.tv_usec = 0;

        // 添加一个元素
        // FD_SET(socketServer, &allSockets);
        // 删除一个元素
        // FD_CLR(socketServer, &allSockets);
        // 判断socket是否在集合中，不在返回0，在则返回非0
        // FD_ISSET(socketServer, &allSockets);

        // 清空并设置文件描述符集合
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(m_listenFd, &readfds);

        SOCKET maxFd = m_listenFd;

        // 构建文件描述符集合
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& client : clients)
            {
                if (client.socket != INVALID_SOCKET)
                {
                    FD_SET(client.socket, &readfds);
                    if (client.needWrite)
                    {
                        FD_SET(client.socket, &writefds);
                    }
                    if (client.socket > maxFd)
                    {
                        maxFd = client.socket;
                    }
                }
            }
        }

        // 监控读写事件
        int activity = select(maxFd + 1, &readfds, &writefds, NULL, &timeout);
        if (activity == SOCKET_ERROR)
        {
            LOG_ERROR("select error: %d", WSAGetLastError());
            break;
        }

        // 处理新连接
        if (FD_ISSET(m_listenFd, &readfds))
        {
            HandleNewConnection(clients, clientsMutex);
        }
        // 处理客户端活动
        ProcessClientActivities(clients, readfds, writefds, clientsMutex);
    }

    // 清理
    CleanupClients(clients);
}

void webServer::ProcessHttpRequest(ClientInfo& client)
{
    HttpConnect hconn;
    hconn.ParseRequest(client.readBuff.GetValidDataToStr());
    std::string httpResponse = hconn.GenerateResponse();
    client.writeBuff.Append(httpResponse.data(), httpResponse.size());
}

void webServer::HandleNewConnection(std::vector<ClientInfo>& clients, std::mutex& clientsMutex)
{
    SOCKET newSocket = accept(m_listenFd, NULL, NULL);
    if (newSocket == INVALID_SOCKET)
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            LOG_ERROR("accept failed: %d", err);
        }
        return;
    }

    // 设置非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(newSocket, FIONBIO, &mode) == SOCKET_ERROR)
    {
        LOG_ERROR("ioctlsocket failed: %d", WSAGetLastError());
        closesocket(newSocket);
        return;
    }

    // 添加到客户端数组
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& client : clients)
    {
        if (client.socket == INVALID_SOCKET)
        {
            client.needWrite = false;
            client.socket    = newSocket;
            client.readBuff.Reset();
            client.writeBuff.Reset();
            LOG_INFO("New connection: socket %d", newSocket);
            return;
        }
    }
    LOG_INFO("Too many connections, closing new socket");
    closesocket(newSocket);
}

void webServer::ProcessClientActivities(std::vector<ClientInfo>& clients, fd_set& readfds, fd_set& writefds, std::mutex& clientsMutex)
{
    std::lock_guard<std::mutex> lock(clientsMutex);

    for (size_t i = 0; i < clients.size(); ++i)
    {
        auto& client = clients[i];
        if (client.socket == INVALID_SOCKET) continue;

        // 处理读事件
        if (FD_ISSET(client.socket, &readfds))
        {
            if (!HandleClientRead(client))
            {
                continue;
            }
        }

        // 处理写事件
        if (FD_ISSET(client.socket, &writefds) && client.needWrite)
        {
            if (!HandleClientWrite(client))
            {
                continue;
            }
        }
    }
}

bool webServer::HandleClientRead(ClientInfo& client)
{
    char clientData[TEMP_BUFFER_SIZE]{0};

    int recvResult = recv(client.socket,
                          clientData,
                          TEMP_BUFFER_SIZE,
                          0);

    if (recvResult == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            LOG_ERROR("recv failed: %d, closing socket %d", err, client.socket);
            closesocket(client.socket);
            client.socket = INVALID_SOCKET;
        }
        return false;
    }
    else if (recvResult == 0)
    {
        LOG_INFO("Connection closed by client, socket %d", client.socket);
        closesocket(client.socket);
        client.socket = INVALID_SOCKET;
        return false;
    }

    client.readBuff.Append(clientData, recvResult);

    // 处理完整请求
    if (util::StringUtil::Find(client.readBuff.GetValidData(), client.readBuff.ValidLength(), "\r\n\r\n", strlen("\r\n\r\n")) != nullptr)
    {
        // 使用线程池处理请求
        auto ret = m_threadPool->enqueue([this, &client]() {
            ProcessHttpRequest(client);
            client.needWrite = true;
            client.readBuff.Reset();
        });
    }
    return true;
}

bool webServer::HandleClientWrite(ClientInfo& client)
{
    int sendResult = send(client.socket,
                          client.writeBuff.GetValidData(),
                          client.writeBuff.ValidLength(),
                          0);

    if (sendResult == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            LOG_ERROR("send failed: %d, closing socket %d", err, client.socket);
            closesocket(client.socket);
            client.socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    client.writeBuff.AddReadPos(sendResult);

    if (client.writeBuff.ValidLength() < 0)
    {
        client.needWrite = false;
        client.writeBuff.Reset();
        closesocket(client.socket);
        client.socket = INVALID_SOCKET;
        LOG_ERROR("send response exception, connection closed");
        return false;
    }
    else if (client.writeBuff.ValidLength() == 0) // 写入完成
    {
        client.needWrite = false;
        client.writeBuff.Reset();
    }
    return true;
}

void webServer::CleanupClients(std::vector<ClientInfo>& clients)
{
    for (auto& client : clients)
    {
        if (client.socket != INVALID_SOCKET)
        {
            closesocket(client.socket);
        }
    }
}

std::string webServer::GetClientIP(SOCKET socket)
{
    // 获取客户端IP
    char Buffer[22] = {0};

    sockaddr_in addr = {0};
    int addrLen      = sizeof(addr);

    if (getpeername(socket, (sockaddr*)&addr, &addrLen) == SOCKET_ERROR)
    {
        return "";
    }

    DWORD ipStrLength = static_cast<DWORD>(sizeof(Buffer));
    if (WSAAddressToStringA(
            (LPSOCKADDR)&addr,
            sizeof(addr),
            NULL,
            Buffer,
            &ipStrLength)
        == 0)
    {
        return std::string(Buffer, sizeof(Buffer));
    }
    return "";
}

bool webServer::InitSocket()
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
