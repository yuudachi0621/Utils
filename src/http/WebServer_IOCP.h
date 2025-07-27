#pragma once
#include "VariableBuffer.h"
#include "WinNetworkDef.h"
#include "threadPool.h"
#include <atomic>
#include <thread>
#include <unordered_set>

struct ConnectionContext : public std::enable_shared_from_this<ConnectionContext>
{
    static std::shared_ptr<ConnectionContext> Create()
    {
        return std::shared_ptr<ConnectionContext>(new ConnectionContext());
    }

    ~ConnectionContext()
    {
        if (socket != INVALID_SOCKET)
            closesocket(socket);
    }

    OVERLAPPED overlapped;      // 异步 I/O操作的基础结构
    WSABUF wsaBuf;              // 异步 I/O 操作的数据缓冲区
    bool isReadPending = false; // 当前连接的操作状态

    SOCKET socket = INVALID_SOCKET;
    VariableBuffer readBuff;
    VariableBuffer writeBuff;

private:
    ConnectionContext() // 禁止外部构造
        : socket(INVALID_SOCKET), isReadPending(false), readBuff(1024 * 4), writeBuff(1024 * 6)
    {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

class WebServer_IOCP
{
public:
    WebServer_IOCP(int port,
                   bool OptLinger,
                   int sqlPort,
                   const char* sqlUser,
                   const char* sqlPwd,
                   const char* dbName,
                   int connPoolNum,
                   int threadNum,
                   bool openLog = true,
                   int logLevel = 0);

    ~WebServer_IOCP();

    void Start();

private:
    bool InitSocket();

    bool InitIOCP(int threadNum);

    void WorkerThread();

    void ProcessClientData(std::shared_ptr<ConnectionContext> context);

    void ProcessHttpRequest(std::shared_ptr<ConnectionContext> context);

    void HandleNewConnection(std::shared_ptr<ConnectionContext> context);

    // 投递AcceptEx请求
    void PostAccept();

    // 投递读写操作
    void PostRead(std::shared_ptr<ConnectionContext> context);
    void PostWrite(std::shared_ptr<ConnectionContext> context);

    void CloseConnection(std::shared_ptr<ConnectionContext> context);

private:
#ifdef _MSC_VER
    WSAInit m_wsaInit; // Windows平台需要初始化 Winsock
    HANDLE m_iocpHandle;
#endif

    int m_port;       // Server监听端口
    int m_listenFd;   // Server Fd
    bool m_openLiger; // 优雅关闭
    bool m_isRunning; // Server是否运行

    std::unique_ptr<util::ThreadPool> m_threadPool;                             // 线程池
    std::unordered_set<std::shared_ptr<ConnectionContext>> m_activeConnections; // 全局连接表
    std::mutex m_connectionsMutex;                                              // 互斥锁
};