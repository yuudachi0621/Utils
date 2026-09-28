#include "log.h"
#include "FileUtil.h"

#include <chrono>

namespace util {

Log::Log() = default;

Log::~Log()
{
    // 先停止接收新的异步日志，再等待写线程排空队列。
    m_isRunning.store(false, std::memory_order_release);

    if (m_writeThread.joinable())
        m_writeThread.join();

    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_fp)
    {
        fflush(m_fp);
        fclose(m_fp);
        m_fp = nullptr;
    }
}

Log& Log::GetInstance()
{
    static Log instance;
    return instance;
}

void Log::FlushLogThread()
{
    Log::GetInstance().AsyncWrite();
}

bool Log::Init(int level, const std::string& path, const std::string& suffix, bool isAsync)
{
    // 支持重新初始化：先停止旧写线程并关闭旧文件。
    m_isRunning.store(false, std::memory_order_release);
    if (m_writeThread.joinable())
        m_writeThread.join();

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_fp)
        {
            fflush(m_fp);
            fclose(m_fp);
            m_fp = nullptr;
        }

        m_path      = path;
        m_suffix    = suffix;
        m_toDay     = Timer::Days();
        m_lineCount = 0;
    }

    m_level.store(level, std::memory_order_relaxed);
    m_isAsync.store(isAsync, std::memory_order_relaxed);

    if (!FileUtil::CreateFolder(m_path))
        return false;

    const std::string logFileName = m_path + "/" + Timer::FormatYMD("%04d_%02d_%02d") + m_suffix;
    FILE* fp                      = fopen(logFileName.c_str(), "a");
    if (!fp)
        return false;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_fp = fp;
    }

    // 先置运行标志再启动写线程，避免线程启动后立即退出。
    m_isRunning.store(true, std::memory_order_release);
    if (m_isAsync.load(std::memory_order_relaxed))
    {
        try
        {
            m_writeThread = std::thread(&Log::AsyncWrite, this);
        } catch (...)
        {
            m_isRunning.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_mtx);
            fclose(m_fp);
            m_fp = nullptr;
            return false;
        }
    }

    return true;
}

void Log::Flush()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_fp)
        fflush(m_fp);
}

void Log::SetLevel(int level)
{
    m_level.store(level, std::memory_order_relaxed);
}

int Log::GetLevel() const
{
    return m_level.load(std::memory_order_relaxed);
}

bool Log::IsOpen() const
{
    return m_isRunning.load(std::memory_order_relaxed);
}

const char* Log::LevelTitle(int level) noexcept
{
    static const char* kLevelTitles[] = {
        "[DEBUG]: ",
        "[INFO] : ",
        "[WARN] : ",
        "[ERROR]: ",
        "[FATAL]: ",
    };

    const int validLevel = (level >= 0 && level < static_cast<int>(std::size(kLevelTitles))) ? level : 1;
    return kLevelTitles[validLevel];
}

void Log::WriteSync(std::string&& message)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_fp)
        return;

    RotateFileIfNeededLocked();
    if (!m_fp)
        return;

    fwrite(message.data(), 1, message.size(), m_fp);
    fflush(m_fp);
}

void Log::RotateFileIfNeededLocked()
{
    const bool dateChanged = (Timer::Days() != m_toDay);
    const bool lineFull    = (m_lineCount > 0 && m_lineCount % LOG_MAX_LINES == 0);
    if (!dateChanged && !lineFull)
        return;

    if (m_fp)
    {
        fflush(m_fp);
        fclose(m_fp);
        m_fp = nullptr;
    }

    std::string newLogFileName;
    if (dateChanged)
    {
        newLogFileName = m_path + "/" + Timer::FormatYMD("%04d_%02d_%02d") + m_suffix;
        m_toDay        = Timer::Days();
        m_lineCount    = 0;
    }
    else
    {
        newLogFileName = StringUtil::Format("%s/%s_%d%s",
                                            m_path.c_str(),
                                            Timer::FormatYMD("%04d_%02d_%02d").c_str(),
                                            (m_lineCount / LOG_MAX_LINES) + 1,
                                            m_suffix.c_str());
    }

    m_fp = fopen(newLogFileName.c_str(), "a");
    if (!m_fp)
        m_isRunning.store(false, std::memory_order_release);
}

void Log::AsyncWrite()
{
    size_t pendingFlush = 0;

    // 即使停止标志已经置位，也要先排空队列，避免退出时丢失已入队日志。
    while (m_isRunning.load(std::memory_order_acquire) || !m_queue.empty())
    {
        std::string message;
        if (!m_queue.try_pop(message))
        {
            if (!m_isRunning.load(std::memory_order_acquire) && m_queue.empty())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_fp)
                continue;

            RotateFileIfNeededLocked();
            if (!m_fp)
                continue;

            fwrite(message.data(), 1, message.size(), m_fp);
            ++m_lineCount;

            // 异步日志按批刷新，减少 fflush 对吞吐量的影响。
            if (++pendingFlush >= ASYNC_FLUSH_BATCH)
            {
                fflush(m_fp);
                pendingFlush = 0;
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_fp)
        fflush(m_fp);
}

} // namespace util
