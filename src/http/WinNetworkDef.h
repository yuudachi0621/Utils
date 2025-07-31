#pragma once

#ifdef _MSC_VER

#define NOMINMAX            // 禁用 min/max 宏
#define WIN32_LEAN_AND_MEAN // 减少 Windows 头文件的冗余内容

#include <WinSock2.h>
#include <stdexcept>
#pragma comment(lib, "ws2_32.lib")  // socket
#pragma comment(lib, "Mswsock.lib") // IOCP

class WSAInit
{
public:
    WSAInit()
    {
        WSADATA swaData;
        if (0 != WSAStartup(MAKEWORD(2, 2), &swaData))
        {
            throw std::invalid_argument("WSAInit failed\n");
        }
    }
    ~WSAInit()
    {
        WSACleanup();
    }
};
#endif