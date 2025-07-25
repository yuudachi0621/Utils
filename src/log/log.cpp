#include "log.h"
#include "FileUtil.h"

namespace util {

Log::Log()
{
    m_toDay     = 0;
    m_lineCount = 0;
    m_level     = 0;
    m_isAsync   = false;
    m_isOpen    = false;
    m_fp        = nullptr;
}

Log::~Log()
{
    m_isOpen = false;
    if (m_isAsync)
    {
        while (!m_queue.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };

        if (m_writeThread.joinable())
            m_writeThread.join();
    }

    if (m_fp)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        Flush();
        fclose(m_fp);
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
    m_level   = level;
    m_isOpen  = true;
    m_isAsync = isAsync;

    m_path                  = path;
    m_suffix                = suffix;
    m_toDay                 = Timer::Days();
    std::string logFileName = m_path + "/" + Timer::FormatYMD("%04d_%02d_%02d") + m_suffix;

    if (m_isAsync)
    {
        std::thread temp(FlushLogThread);
        std::swap(m_writeThread, temp);
    }

    {
        std::lock_guard<std::mutex> locker(m_mtx);
        m_logBuff.Reset();
        if (nullptr != m_fp)
        {
            Flush();
            fclose(m_fp);
        }

        m_fp = fopen(logFileName.data(), "a");
        if (nullptr == m_fp)
        {
            if (!FileUtil::CreateFolder(m_path))
                return false;

            m_fp = fopen(logFileName.data(), "a");
            if (nullptr == m_fp)
                return false;
        }
    }
    return true;
}

void Log::Flush()
{
    fflush(m_fp);
}

int Log::GetLevel()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_level;
}

void Log::SetLevel(int level)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    m_level = level;
}

bool Log::IsOpen() const
{
    return m_isOpen;
}

void Log::AppendLogLevelTitle(int level)
{
    static const char* level_title[] = {"[DEBUG]: ", "[INFO] : ", "[WARN] : ", "[ERROR]: ", "[FATAL]: "};
    int valid_level                  = (level >= 0 && level <= 4) ? level : 1;
    m_logBuff.Append(level_title[valid_level], 9);
}

void Log::AsyncWrite()
{
    while (m_isAsync)
    {
        if (m_queue.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::lock_guard<std::mutex> lock(m_mtx);
        std::string logMessage = m_queue.pop();

        fputs(logMessage.c_str(), m_fp);
        fflush(m_fp); // »∑±£–¥»Î¥≈≈Ã
    }
}

} // namespace util
