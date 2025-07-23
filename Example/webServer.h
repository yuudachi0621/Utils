#pragma once
#include "threadPool.h"

#ifdef _MSC_VER
#define NOMINMAX            // 禁用 min/max 宏
#define WIN32_LEAN_AND_MEAN // 减少 Windows 头文件的冗余内容
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

class WSAInit
{
public:
    WSAInit()
    {
        WSADATA swaData;
        int result = WSAStartup(MAKEWORD(2, 2), &swaData);
        if (result != 0)
        {
            // handle error
            return;
        }
    }
    ~WSAInit()
    {
        WSACleanup();
    }
};
#endif

constexpr auto BUFFER_SIZE = 4096;
typedef struct ClientInfo
{
    SOCKET socket;
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    bool needWrite; // 是否需要监控写事件
    char response[BUFFER_SIZE];
    int responseLength;
    int bytesSent;
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
        int threadNum);

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

    void GenerateHttpResponse(ClientInfo& client, const char* timeStr, const char* ipStr);

    void SendErrorResponse(ClientInfo& client, int code, const char* message);

    bool GetClientIP(SOCKET socket, char* buffer, size_t bufferSize);

private:
    bool GetNowTime(char* buffer, size_t bufferSize);

private:
#ifdef _MSC_VER
    WSAInit m_wsaInit; // Windows平台需要初始化 Winsock
#endif

    int m_port;       // Server监听端口
    int m_listenFd;   // Server Fd
    bool m_openLiger; // 优雅关闭
    bool m_isClose;   // Server是否关闭

    std::string m_srcPath; // 资源存放路径

    std::unique_ptr<util::ThreadPool> m_threadPool; // 线程池
};
