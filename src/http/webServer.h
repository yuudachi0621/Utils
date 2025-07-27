#pragma once
#include "VariableBuffer.h"
#include "WinNetworkDef.h"
#include "threadPool.h"
#include <atomic>

static constexpr auto TEMP_BUFFER_SIZE = 4096;
typedef struct ClientInfo
{
    SOCKET socket;
    std::atomic<bool> needWrite; // 是否需要监控写事件
    VariableBuffer readBuff;
    VariableBuffer writeBuff;
};

class webServer
{
    static const int webServerMaxCount = 30;

public:
    webServer(
        int port,
        bool OptLinger,
        int sqlPort,
        const char* sqlUser,
        const char* sqlPwd,
        const char* dbName,
        int connPoolNum,
        int threadNum,
        bool openLog = true,
        int logLevel = 0);

    ~webServer();

    void Start();

private:
    bool InitSocket();

    void HandleNewConnection(std::vector<ClientInfo>& clients, std::mutex& clientsMutex);

    void ProcessClientActivities(std::vector<ClientInfo>& clients,
                                 fd_set& readfds,
                                 fd_set& writefds,
                                 std::mutex& clientsMutex);

    bool HandleClientRead(ClientInfo& client);

    bool HandleClientWrite(ClientInfo& client);

    void ProcessHttpRequest(ClientInfo& client);

    void CleanupClients(std::vector<ClientInfo>& clients);

    std::string GetClientIP(SOCKET socket);

private:
#ifdef _MSC_VER
    WSAInit m_wsaInit; // Windows平台需要初始化 Winsock
#endif

    int m_port;       // Server监听端口
    int m_listenFd;   // Server Fd
    bool m_openLiger; // 优雅关闭

    std::unique_ptr<util::ThreadPool> m_threadPool; // 线程池
};
