#pragma once
#include "VariableBuffer.h"
#include "WinNetworkDef.h"
#include "safeQueue.h"
#include "threadPool.h"
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>

struct ConnectionContext : public std::enable_shared_from_this<ConnectionContext>
{
    static std::shared_ptr<ConnectionContext> Create()
    {
        return std::shared_ptr<ConnectionContext>(new ConnectionContext());
    }

    void SafeClose()
    {
        if (socket != INVALID_SOCKET)
        {
            ::shutdown(socket, SD_SEND);
            char tmp[1024];
            while (recv(socket, tmp, sizeof(tmp), 0) > 0) {} // 读完剩余
            ::closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }

    ~ConnectionContext()
    {
        SafeClose();
    }

    OVERLAPPED readOv{};
    OVERLAPPED writeOv{};
    WSABUF wsaBuf{};
    char acceptBuffer[64]{0};
    std::atomic<bool> closed{false};

    SOCKET socket = INVALID_SOCKET;
    VariableBuffer readBuff{16 * 1024};
    util::SafeQueue<std::vector<char>> sendQueue;

private:
    ConnectionContext()
        : socket(INVALID_SOCKET) {}
};

enum IocpKey : ULONG_PTR
{
    kAcceptKey = 1,
    kConnKey   = 2,
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

    bool InitIOCP();

    void WorkerThread();

    void ProcessClientData(std::shared_ptr<ConnectionContext> context);

    void HandleNewConnection(std::shared_ptr<ConnectionContext> context);

    // 投递AcceptEx请求
    void PostAccept();

    // 投递读写操作
    void PostRead(std::shared_ptr<ConnectionContext> context);
    void PostWrite(std::shared_ptr<ConnectionContext> context);

    void CloseConnection(std::shared_ptr<ConnectionContext> context);

    std::shared_ptr<ConnectionContext> FindContextByRaw(ConnectionContext* raw);

private:
#ifdef _MSC_VER
    WSAInit m_wsaInit; // Windows平台需要初始化 Winsock
    HANDLE m_iocpHandle;
#endif

    SOCKET m_listenFd; // Server Fd
    int m_port;        // Server监听端口
    bool m_openLiger;  // 优雅关闭
    bool m_isRunning;  // Server是否运行

    std::vector<std::thread> m_workerThreads;                                                       // WorkerThread()
    std::shared_ptr<util::ThreadPool> m_threadPool;                                                 // HTTP处理
    std::unordered_map<ConnectionContext*, std::shared_ptr<ConnectionContext>> m_activeConnections; // 全局连接表
    std::mutex m_connectionsMutex;                                                                  // 互斥锁
};