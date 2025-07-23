#ifndef __LOG__H__
#define __LOG__H__

#include "safeQueue.h"
#include <mutex>
#include <string>

namespace util {
class Log
{
protected:
    Log();
    ~Log();

public:
    static Log& GetInstance()
    {
        static Log instance;
        return instance;
    }
    static void FlushLogThread();

    bool init(int level, const std::string& path = "./log", const std::string& suffix = ".log", int maxQueueCapacity = 1024);
    void write(int level, const std::string& format, ...);
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen();

private:
    void AppendLogLevelTitle(int level);
    void AsyncWrite();

private:
    static const int LOG_PATH_LEN  = 256;
    static const int LOG_NAME_LEN  = 256;
    static const int LOG_MAX_LINES = 50000;

    std::string m_path;
    std::string m_suffix;
    int m_toDay;
    int m_lineCount;

    bool m_isOpen;
    int m_level;
    FILE* m_fp;
    std::mutex m_mtx;
    SafeQueue<std::string> m_queue;
};
} // namespace util
#endif