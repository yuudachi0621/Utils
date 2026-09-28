#ifndef __LOG__H__
#define __LOG__H__

#include "safeQueue.h"
#include "stringUtil.h"
#include "timer.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace util {

/**
 * 线程安全日志实现。
 *
 * 同步模式：调用线程直接写文件。
 * 异步模式：调用线程只负责格式化和入队，单独写线程批量落盘。
 *
 * 关键设计：
 * - m_queue 使用非阻塞 try_pop，写线程可响应停止标志优雅退出；
 * - 格式化在锁外完成，缩短多线程日志的临界区；
 * - 文件轮转、m_fp、行数统计统一由 m_mtx 保护；
 * - 异步模式按批次 flush，避免每条日志都触发磁盘刷新。
 */
class Log
{
private:
    static constexpr int LOG_MAX_LINES        = 10000;
    static constexpr size_t ASYNC_FLUSH_BATCH = 64;

public:
    static Log& GetInstance();
    static void FlushLogThread();

    // 初始化日志系统。路径不存在时会尝试创建目录。
    bool Init(int level, const std::string& path = "./log", const std::string& suffix = ".log", bool isAsync = true);

    /**
     * 写入日志。
     *
     * 调用线程负责：
     * 1. 根据运行状态和等级快速过滤；
     * 2. 在锁外完成时间、等级和正文格式化；
     * 3. 同步模式直接写文件，异步模式只入队。
     */
    template <typename... Args>
    void WriteLog(int level, const std::string& format, Args&&... args)
    {
        if (!m_isRunning.load(std::memory_order_relaxed) || level < m_level.load(std::memory_order_relaxed))
            return;

        std::string message;
        message.reserve(256);
        message += Timer::FormatYMDHMSS("%d-%02d-%02d %02d:%02d:%02d.%03d ");
        message += LevelTitle(level);
        message += StringUtil::Format(format.c_str(), std::forward<Args>(args)...);
        message += '\n';

        if (m_isAsync.load(std::memory_order_relaxed))
        {
            m_queue.push(std::move(message));
        }
        else
        {
            WriteSync(std::move(message));
        }
    }

    // 将用户态日志缓冲区刷新到文件。
    void Flush();

    void SetLevel(int level);
    int GetLevel() const;

    bool IsOpen() const;

protected:
    Log();
    ~Log();

private:
    static const char* LevelTitle(int level) noexcept;

    void WriteSync(std::string&& message);
    void RotateFileIfNeededLocked();
    void AsyncWrite();

private:
    std::string m_path;
    std::string m_suffix;

    // 以下字段只在 m_mtx 保护下读写。
    int m_toDay     = 0;
    int m_lineCount = 0;
    FILE* m_fp      = nullptr;
    mutable std::mutex m_mtx;

    std::atomic<int> m_level{0};
    std::atomic<bool> m_isAsync{false};
    std::atomic<bool> m_isRunning{false};

    std::thread m_writeThread;
    SafeQueue<std::string> m_queue;
};

// 只保留一次日志入口调用；WriteLog 内部负责等级过滤。
#define LOG_BASE(level, format, ...)                               \
    do                                                             \
    {                                                              \
        Log::GetInstance().WriteLog(level, format, ##__VA_ARGS__); \
    } while (0);

#define LOG_DEBUG(format, ...)             \
    do                                     \
    {                                      \
        LOG_BASE(0, format, ##__VA_ARGS__) \
    } while (0);

#define LOG_INFO(format, ...)              \
    do                                     \
    {                                      \
        LOG_BASE(1, format, ##__VA_ARGS__) \
    } while (0);

#define LOG_WARN(format, ...)              \
    do                                     \
    {                                      \
        LOG_BASE(2, format, ##__VA_ARGS__) \
    } while (0);

#define LOG_ERROR(format, ...)             \
    do                                     \
    {                                      \
        LOG_BASE(3, format, ##__VA_ARGS__) \
    } while (0);

} // namespace util
#endif
