#pragma once
#include "VariableBuffer.h"
#include "WinNetworkDef.h"
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
            ::shutdown(socket, SD_BOTH);
            ::closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }

    ~ConnectionContext()
    {
        SafeClose();
    }

    OVERLAPPED readOv{};  // 读的Overlapped
    OVERLAPPED writeOv{}; // 写的Overlapped
    WSABUF wsaBuf;        // 异步 I/O 操作的数据缓冲区
    char acceptBuffer[64]{0};
    std::atomic<bool> closed{false};

    SOCKET socket = INVALID_SOCKET;
    VariableBuffer readBuff;
    VariableBuffer writeBuff;

private:
    ConnectionContext()
        : socket(INVALID_SOCKET), readBuff(1024 * 8), writeBuff(1024 * 8)
    {
    }
};

enum IocpKey : ULONG_PTR
{
    kAcceptKey   = 1,
    kConnKey     = 2,
    kWriteReqKey = 3 // 写请求
};

struct WriteRequest
{
    std::shared_ptr<ConnectionContext> context;
    std::string responseData;

    WriteRequest(std::shared_ptr<ConnectionContext> ctx, std::string&& data)
        : context(std::move(ctx)), responseData(std::move(data)) {}
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

    void ProcessHttpRequest(std::shared_ptr<ConnectionContext> context, const std::string& httpRequest);

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

    int m_port;       // Server监听端口
    int m_listenFd;   // Server Fd
    bool m_openLiger; // 优雅关闭
    bool m_isRunning; // Server是否运行

    std::vector<std::thread> m_workerThreads;
    std::shared_ptr<util::ThreadPool> m_threadPool;
    std::unordered_set<std::shared_ptr<ConnectionContext>> m_activeConnections; // 全局连接表
    std::mutex m_connectionsMutex;                                              // 互斥锁
};