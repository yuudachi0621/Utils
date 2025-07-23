#include "log.h"

namespace util {

Log::Log()
{
    m_fp        = nullptr;
    m_toDay     = 0;
    m_lineCount = 0;
    m_isOpen    = false;
    m_level     = 0;
}

Log::~Log()
{
}

void Log::FlushLogThread()
{
}

bool Log::init(int level, const std::string& path, const std::string& suffix, int maxQueueCapacity)
{
    return false;
}

void Log::write(int level, const std::string& format, ...)
{
}

void Log::flush()
{
}

int Log::GetLevel()
{
    return 0;
}

void Log::SetLevel(int level)
{
}

bool Log::IsOpen()
{
    return false;
}

void Log::AppendLogLevelTitle(int level)
{
}

void Log::AsyncWrite()
{
}

} // namespace util
