#pragma once
#include "VariableBuffer.h"
#include "WinNetworkDef.h"
#include "ThreadPool.h"
#include "HttpResponse.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct ConnectionContext;

/**
 * 一次异步 I/O 的操作类型。
 *
 * 每种 I/O 都使用独立的 OVERLAPPED，原因是：
 * - 同一个 OVERLAPPED 不能被多个未完成请求同时使用；
 * - 读写可能并发进行，必须能通过完成包准确识别操作类型。
 */
enum class IoOperation : unsigned char
{
    Accept,
    Read,
    Write,
};

/**
 * OVERLAPPED 的业务包装。
 *
 * IOCP 完成包只返回 lpOverlapped，通过 CONTAINING_RECORD 反推本结构后，
 * 即可得到操作类型和连接上下文指针。
 */
struct IoOverlapped
{
    OVERLAPPED overlapped{};
    IoOperation operation    = IoOperation::Read;
    ConnectionContext* owner = nullptr;
};

/**
 * 单条 TCP 连接的全部状态。
 *
 * 生命周期原则：
 * - 同一时间最多一个未完成 WSARecv；
 * - 同一时间最多一个未完成 WSASend；
 * - 同一时间最多一个 HTTP 解析任务；
 * - closed 只表示逻辑关闭，真正释放 socket 必须等待 pendingIo 归零；
 * - currentWrite 在发送完成前不能释放或替换。
 */
struct ConnectionContext : public std::enable_shared_from_this<ConnectionContext>
{
    static std::shared_ptr<ConnectionContext> Create()
    {
        return std::shared_ptr<ConnectionContext>(new ConnectionContext());
    }

    // 真正销毁上下文前关闭 socket；不执行阻塞式 recv 排空。
    void CloseSocketNoThrow() noexcept
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
        CloseSocketNoThrow();
    }

    // 已 Accept 的客户端 socket；AcceptEx 尚未完成时也可能暂时是预备 socket。
    SOCKET socket = INVALID_SOCKET;

    // 逻辑状态。原子变量用于 IOCP 工作线程、业务线程和关闭流程之间的同步。
    std::atomic<bool> closed{false};          // 连接是否已进入关闭流程
    std::atomic<bool> closeAfterWrite{false}; // 响应写完后主动关闭，用于 Connection: close
    std::atomic<bool> readInProgress{false};  // 是否已有未完成的 WSARecv
    std::atomic<bool> writeInProgress{false}; // 是否已有未完成的 WSASend
    std::atomic<bool> processing{false};      // 是否已有 HTTP 解析任务
    std::atomic<bool> accepting{false};       // 是否正在等待 AcceptEx 完成
    std::atomic<int> pendingIo{0};            // 未完成异步操作计数，用于延迟释放

    // Accept/Read/Write 各自独立的 OVERLAPPED，避免完成包串线。
    IoOverlapped acceptIo{};
    IoOverlapped readIo{};
    IoOverlapped writeIo{};

    // WSARecv/WSASend 会引用这两个描述符，因此保持为连接成员，不能作为临时局部变量。
    WSABUF readWsaBuf{};
    std::array<WSABUF, 2> writeWsaBufs{}; // header + body/file 两组 scatter-gather 缓冲区
    ULONG writeBufferCount = 0;

    // AcceptEx 要求接收地址缓冲区；IPv4 地址长度为 sizeof(sockaddr_in) + 16。
    std::array<char, 2 * (sizeof(sockaddr_in) + 16)> acceptBuffer{};

    // 读取缓冲区。解析任务未完成前不会发起下一次读，因此不会并发修改。
    VariableBuffer readBuff{64 * 1024};

    // 发送队列和当前发送缓冲区。
    // sendQueue 由业务线程投递，currentWrite 由 IOCP 完成回调消费。
    std::mutex writeMutex;
    std::deque<HttpResponsePacket> sendQueue;
    HttpResponsePacket currentWrite;
    size_t currentWriteOffset = 0;

private:
    ConnectionContext() = default;
};

/**
 * IOCP completion key。
 *
 * kAcceptKey: 监听 socket 的 AcceptEx 完成
 * kConnKey:   客户端 socket 的读写完成
 * kShutdownKey: 析构时唤醒工作线程的显式停止包
 */
enum IocpKey : ULONG_PTR
{
    kAcceptKey   = 1,
    kConnKey     = 2,
    kShutdownKey = 3,
};

/**
 * 基于 Windows IOCP 的 HTTP/1.x 服务器。
 *
 * 核心并发模型：
 * - 监听 socket 关联 IOCP，预投递多个 AcceptEx；
 * - 每个客户端 socket 关联同一 IOCP；
 * - IOCP 工作线程只处理完成事件，不执行阻塞式 HTTP 业务；
 * - HTTP 解析和业务处理交给 ThreadPool；
 * - 连接对象通过 shared_ptr + pendingIo 计数保证异步回调期间的生命周期安全。
 */
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

    // 启动 AcceptEx 预投递并进入服务循环；该调用会阻塞直到对象析构。
    void Start();

private:
    // 初始化监听 socket、完成端口和工作线程。
    bool InitSocket();
    bool InitIOCP();

    // IOCP 工作线程和完成事件分派。
    void WorkerThread();
    void HandleCompletion(DWORD bytesTransferred, LPOVERLAPPED overlapped);

    // 各类 I/O 完成后的状态推进。
    void HandleAccept(const std::shared_ptr<ConnectionContext>& context, DWORD bytesTransferred, BOOL success);
    void HandleRead(const std::shared_ptr<ConnectionContext>& context, DWORD bytesTransferred, BOOL success);
    void HandleWrite(const std::shared_ptr<ConnectionContext>& context, DWORD bytesTransferred, BOOL success);

    // HTTP 解析。每条连接同一时间只允许一个解析任务。
    void ProcessClientData(const std::shared_ptr<ConnectionContext>& context);
    void ProcessRequests(const std::shared_ptr<ConnectionContext>& context);

    // 异步 I/O 投递。
    void PostAccept();
    void PostRead(const std::shared_ptr<ConnectionContext>& context);
    void PostWrite(const std::shared_ptr<ConnectionContext>& context);
    void EnqueueResponse(const std::shared_ptr<ConnectionContext>& context, HttpResponsePacket&& response);

    // 生命周期管理。
    void CloseConnection(const std::shared_ptr<ConnectionContext>& context);
    void FinishIo(const std::shared_ptr<ConnectionContext>& context);
    void RegisterConnection(const std::shared_ptr<ConnectionContext>& context);
    void UnregisterConnection(const std::shared_ptr<ConnectionContext>& context);
    std::shared_ptr<ConnectionContext> FindContextByRaw(ConnectionContext* raw);

private:
    // 预投递 AcceptEx 数量。每完成一个 AcceptEx 会立即补投一个，维持监听能力。
    static constexpr size_t kPrePostAccept = 256;

    // 每次从完成端口批量取出的最大完成包数量。
    static constexpr size_t kCompletionBatch = 64;

    // 协议限制，防止单连接无限占用内存。
    static constexpr size_t kMaxHeaderBytes = 64 * 1024;
    static constexpr size_t kMaxBodyBytes   = 32 * 1024 * 1024;

#ifdef _MSC_VER
    WSAInit m_wsaInit; // 进程级 Winsock 初始化，RAII 自动调用 WSACleanup
    HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;
#endif

    SOCKET m_listenFd = INVALID_SOCKET; // 监听 socket
    int m_port        = 0;              // 服务端口
    bool m_openLiger  = false;          // 是否开启 linger
    std::atomic<bool> m_isRunning{false};

    // IOCP 工作线程负责 I/O 完成，ThreadPool 负责可能阻塞的 HTTP 业务。
    std::vector<std::thread> m_workerThreads;
    std::shared_ptr<util::ThreadPool> m_threadPool;

    // 活跃连接表。I/O 完成包只携带裸指针，通过该表恢复 shared_ptr。
    std::unordered_map<ConnectionContext*, std::shared_ptr<ConnectionContext>> m_activeConnections;
    mutable std::shared_mutex m_connectionsMutex;
};
