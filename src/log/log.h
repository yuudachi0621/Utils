#ifndef __LOG__H__
#define __LOG__H__

#include "VariableBuffer.h"
#include "safeQueue.h"
#include "stringUtil.h"
#include "timer.h"
#include <mutex>
#include <string>

namespace util {

class Log
{
private:
    static const int LOG_MAX_LINES = 10000; // 日志最大行数

public:
    static Log& GetInstance();
    static void FlushLogThread();

    // 初始化
    bool Init(int level, const std::string& path = "./log", const std::string& suffix = ".log", bool isAsync = true);

    template <typename... Args>
    void WriteLog(int level, const std::string& format, Args... args);

    // 确保数据从用户缓冲区进入内核缓冲区
    void Flush();

    // Level
    void SetLevel(int level);
    int GetLevel();

    bool IsOpen() const;

protected:
    Log();
    ~Log();

private:
    void AppendLogLevelTitle(int level);
    void AsyncWrite();

private:
    std::string m_path;   // 存放路径
    std::string m_suffix; // 后缀名
    int m_toDay;          // 日期
    int m_lineCount;      // 行数
    int m_level;          // 日志等级

    bool m_isAsync;                // 是否异步
    std::atomic<bool> m_isRunning; // 是否运行

    FILE* m_fp;
    std::mutex m_mtx;
    std::thread m_writeThread; // 写线程
    VariableBuffer m_logBuff;
    SafeQueue<std::string> m_queue;
};

template <typename... Args>
void Log::WriteLog(int level, const std::string& format, Args... args)
{
    if (!m_isRunning)
        return;

    // 检查时间和行数
    bool isDateChange = (Timer::Days() != m_toDay);
    if (isDateChange
        || (m_lineCount > 0 && (m_lineCount % LOG_MAX_LINES == 0)))
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        std::string newLogFileName;
        if (isDateChange)
        {
            newLogFileName = m_path + "/" + Timer::FormatYMD("%04d_%02d_%02d") + m_suffix;
            m_toDay        = Timer::Days();
            m_lineCount    = 0;
        }
        else
        {
            newLogFileName = StringUtil::Format("%s/%s_%d%s",
                                                m_path.data(),
                                                Timer::FormatYMD("%04d_%02d_%02d").data(),
                                                (m_lineCount / LOG_MAX_LINES) + 1,
                                                m_suffix.data());
        }
        Flush();
        fclose(m_fp);
        m_fp = fopen(newLogFileName.data(), "a");
    }

    {
        std::unique_lock<std::mutex> locker(m_mtx);
        m_lineCount++;

        // 日志时间
        std::string curTime = util::Timer::FormatYMDHMSS("%d-%02d-%02d %02d:%02d:%02d.%03d ");
        m_logBuff.Append(curTime.data(), curTime.size());
        // 日志等级
        AppendLogLevelTitle(level);
        // 日志内容
        std::string logMessage = util::StringUtil::Format(format.data(), std::forward<Args>(args)...);
        m_logBuff.Append(logMessage.data(), logMessage.size());
        // 日志尾部换行
        m_logBuff.Append("\n\0", 2);

        if (m_isAsync)
        {
            m_queue.push_back(m_logBuff.GetValidDataToStr());
        }
        else
        {
            fputs(m_logBuff.GetValidData(), m_fp);
            Flush();
        }
        m_logBuff.Reset();
    }
}

// 比设定的Level高才记录
#define LOG_BASE(level, format, ...)                                               \
    do {                                                                           \
        if (Log::GetInstance().IsOpen() && Log::GetInstance().GetLevel() <= level) \
        {                                                                          \
            Log::GetInstance().WriteLog(level, format, ##__VA_ARGS__);             \
        }                                                                          \
    } while (0);

#define LOG_DEBUG(format, ...)             \
    do {                                   \
        LOG_BASE(0, format, ##__VA_ARGS__) \
    } while (0);
#define LOG_INFO(format, ...)              \
    do {                                   \
        LOG_BASE(1, format, ##__VA_ARGS__) \
    } while (0);
#define LOG_WARN(format, ...)              \
    do {                                   \
        LOG_BASE(2, format, ##__VA_ARGS__) \
    } while (0);
#define LOG_ERROR(format, ...)             \
    do {                                   \
        LOG_BASE(3, format, ##__VA_ARGS__) \
    } while (0);

} // namespace util
#endif