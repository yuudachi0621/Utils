#include "webServer.h"
#include "sqlConnPool.h"

webServer::webServer(int port, bool OptLinger, int sqlPort, const char* sqlUser, const char* sqlPwd, const char* dbName, int connPoolNum, int threadNum)
{
    m_port       = port;
    m_openLiger  = OptLinger;
    m_threadPool = std::make_unique<util::ThreadPool>(threadNum);
    sqlConnPool::Instance().Init("127.0.0.1", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    if (!InitSocket())
    {
        return;
    }
}

webServer::~webServer()
{
    closesocket(m_listenFd);
    sqlConnPool::Instance().ClosePool();
}

void webServer::Start()
{
    if (!InitSocket())
    {
        printf("Socket initialization failed\n");
        return;
    }

    fd_set readfds, writefds;
    struct timeval timeout;
    ClientInfo clients[webServerMaxCount];

    // 初始化客户端数组
    for (int i = 0; i < webServerMaxCount; i++)
    {
        clients[i].socket    = INVALID_SOCKET;
        clients[i].needWrite = false; // 初始化为不需要写
    }

    // 主循环
    while (1)
    {
        // 每次循环重置timeout
        timeout.tv_sec  = 5; // 5秒
        timeout.tv_usec = 0;

        // 清空并设置文件描述符集合
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(m_listenFd, &readfds);

        SOCKET maxFd = m_listenFd;

        // 添加客户端套接字并找出最大文件描述符
        for (int i = 0; i < webServerMaxCount; i++)
        {
            if (clients[i].socket != INVALID_SOCKET)
            {
                FD_SET(clients[i].socket, &readfds);

                // 只有当需要写时才监控写事件
                if (clients[i].needWrite)
                {
                    FD_SET(clients[i].socket, &writefds);
                }

                if (clients[i].socket > maxFd)
                {
                    maxFd = clients[i].socket;
                }
            }
        }

        // 监控读写事件
        int activity = select(maxFd + 1, &readfds, &writefds, NULL, &timeout);
        if (activity == SOCKET_ERROR)
        {
            printf("select error: %d\n", WSAGetLastError());
            break;
        }

        // 检查监听套接字是否有新连接
        if (FD_ISSET(m_listenFd, &readfds))
        {
            SOCKET newSocket = accept(m_listenFd, NULL, NULL);
            if (newSocket == INVALID_SOCKET)
            {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK)
                {
                    printf("accept failed: %d\n", err);
                }
                continue;
            }

            // 设置新客户端为非阻塞
            u_long clientMode = 1;
            ioctlsocket(newSocket, FIONBIO, &clientMode);

            // 添加到客户端数组
            bool isAdded = false;
            for (int i = 0; i < webServerMaxCount; i++)
            {
                if (clients[i].socket == INVALID_SOCKET)
                {
                    clients[i].socket        = newSocket;
                    clients[i].bytesReceived = 0;
                    clients[i].needWrite     = false;
                    memset(clients[i].buffer, 0, BUFFER_SIZE);
                    isAdded = true;
                    printf("New connection: socket %d\n", newSocket);
                    break;
                }
            }
            if (!isAdded)
            {
                printf("Too many connections, closing new socket\n");
                closesocket(newSocket);
            }
        }

        // 检查客户端活动
        for (int i = 0; i < webServerMaxCount; i++)
        {
            if (clients[i].socket == INVALID_SOCKET) continue;

            // 处理读事件
            if (FD_ISSET(clients[i].socket, &readfds))
            {
                // 接收数据
                int recvResult = recv(clients[i].socket,
                                      clients[i].buffer + clients[i].bytesReceived,
                                      BUFFER_SIZE - clients[i].bytesReceived,
                                      0);

                if (recvResult == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK)
                    {
                        printf("recv failed: %d, closing socket %d\n", err, clients[i].socket);
                        closesocket(clients[i].socket);
                        clients[i].socket = INVALID_SOCKET;
                    }
                    continue;
                }
                else if (recvResult == 0)
                {
                    // 连接关闭
                    printf("Connection closed by client, socket %d\n", clients[i].socket);
                    closesocket(clients[i].socket);
                    clients[i].socket = INVALID_SOCKET;
                    continue;
                }
                else
                {
                    clients[i].bytesReceived += recvResult;

                    // 检查缓冲区是否已满但未收到完整请求
                    if (clients[i].bytesReceived >= BUFFER_SIZE && strstr(clients[i].buffer, "\r\n\r\n") == NULL)
                    {
                        printf("Request too large, closing connection\n");
                        closesocket(clients[i].socket);
                        clients[i].socket = INVALID_SOCKET;
                        continue;
                    }

                    // 检查是否收到完整HTTP请求
                    if (strstr(clients[i].buffer, "\r\n\r\n") != NULL)
                    {
                        // 将请求交给线程池处理
                        m_threadPool->enqueue([this, i, &clients]() {
                            // 处理HTTP请求并生成响应
                            ProcessHttpRequest(clients[i]);
                            // 处理完成后设置需要写标志
                            clients[i].needWrite = true;
                        });
                    }
                }
            }

            // 处理写事件
            if (FD_ISSET(clients[i].socket, &writefds) && clients[i].needWrite)
            {
                // 发送剩余响应数据
                int sendResult = send(clients[i].socket,
                                      clients[i].response + clients[i].bytesSent,
                                      clients[i].responseLength - clients[i].bytesSent,
                                      0);

                if (sendResult == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK)
                    {
                        printf("send failed: %d, closing socket %d\n", err, clients[i].socket);
                        closesocket(clients[i].socket);
                        clients[i].socket = INVALID_SOCKET;
                    }
                }
                else
                {
                    clients[i].bytesSent += sendResult;

                    // 检查是否发送完成
                    if (clients[i].bytesSent >= clients[i].responseLength)
                    {
                        // 重置写状态
                        clients[i].needWrite      = false;
                        clients[i].responseLength = 0;
                        clients[i].bytesSent      = 0;

                        // 关闭连接（简单实现中每次请求后关闭）
                        closesocket(clients[i].socket);
                        clients[i].socket = INVALID_SOCKET;
                        printf("Response sent, connection closed\n");
                    }
                }
            }
        }
    }

    // 清理所有客户端连接
    for (int i = 0; i < webServerMaxCount; i++)
    {
        if (clients[i].socket != INVALID_SOCKET)
        {
            closesocket(clients[i].socket);
        }
    }
}

void webServer::ProcessHttpRequest(ClientInfo& client)
{
    // 解析HTTP请求
    // 这里可以添加更复杂的HTTP请求处理逻辑

    // 示例：生成响应
    const char* body = "Hello, World!";
    const char* header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n";

    // 计算响应总长度
    int bodyLength   = strlen(body);
    int headerLength = snprintf(nullptr, 0, header, bodyLength);
    int totalLength  = headerLength + bodyLength;

    // 检查响应是否超过缓冲区大小
    if (totalLength >= BUFFER_SIZE)
    {
        const char* errorResponse =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Response too large";

        strncpy(client.response, errorResponse, BUFFER_SIZE - 1);
        client.response[BUFFER_SIZE - 1] = '\0';
        client.responseLength            = strlen(client.response);
    }
    else
    {
        // 构建响应
        snprintf(client.response, BUFFER_SIZE, header, bodyLength);
        strncat(client.response, body, BUFFER_SIZE - strlen(client.response) - 1);
        client.responseLength = totalLength;
    }
    client.bytesSent = 0;
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
        return false;
    }

    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, (const char*)&optLinger, sizeof(optLinger));
    if (ret == SOCKET_ERROR)
    {
        closesocket(m_listenFd);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(int));
    if (ret == SOCKET_ERROR)
    {
        closesocket(m_listenFd);
        return false;
    }

    // 绑定 socket
    ret = bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR)
    {
        closesocket(m_listenFd);
        return false;
    }

    // 监听连接
    ret = listen(m_listenFd, 6);
    if (ret == SOCKET_ERROR)
    {
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
