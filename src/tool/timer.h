#ifndef __TIMER__
#define __TIMER__

#include "stringUtil.h"
#include <chrono>
#include <string>

namespace util {

class Timer
{
public:
    static size_t Years()
    {
        auto now               = std::chrono::system_clock::now();          // 获取当前时间点
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now); // 转换为time_t类型
        std::tm local_tm       = *std::localtime(&now_time_t);              // 转换为本地时间
        return local_tm.tm_year + 1900;
    }

    static size_t Months()
    {
        auto now               = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm       = *std::localtime(&now_time_t);
        return local_tm.tm_mon + 1;
    }

    static size_t Days()
    {
        auto now               = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm       = *std::localtime(&now_time_t);
        return local_tm.tm_mday;
    }

    // 年_月_日
    static std::string FormatYMD(const char* fmt = "%d-%02d-%02d")
    {
        auto now               = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm       = *std::localtime(&now_time_t);

        // 年月日时分
        auto curtime = StringUtil::Format(fmt,
                                          local_tm.tm_year + 1900,
                                          local_tm.tm_mon + 1,
                                          local_tm.tm_mday);
        return curtime;
    }

    // 时_分_秒_毫秒
    static std::string FormatHMSS(const char* fmt = "%02d:%02d:%02d.%03d")
    {
        auto now                 = std::chrono::system_clock::now();
        uint64_t dis_millseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                                   - std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
        time_t tt                = std::chrono::system_clock::to_time_t(now);
        auto time_tm             = localtime(&tt);
        return StringUtil::Format(fmt, time_tm->tm_hour, time_tm->tm_min, time_tm->tm_sec, (int)dis_millseconds);
    }

    // 年_月_日_时_分_秒
    static std::string FormatYMDHMS(const char* fmt = "%d-%02d-%02d %02d:%02d:%02d")
    {
        auto now               = std::chrono::system_clock::now();          // 获取当前时间点
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now); // 转换为time_t类型
        std::tm local_tm       = *std::localtime(&now_time_t);              // 转换为本地时间

        // 年月日时分
        auto curtime = StringUtil::Format(fmt,
                                          local_tm.tm_year + 1900,
                                          local_tm.tm_mon + 1,
                                          local_tm.tm_mday,
                                          local_tm.tm_hour,
                                          local_tm.tm_min,
                                          local_tm.tm_sec);
        return curtime;
    }

    // 年_月_日_时_分_秒_毫秒
    static std::string FormatYMDHMSS(const char* fmt = "%d-%02d-%02d %02d:%02d:%02d.%03d")
    {
        auto now                 = std::chrono::system_clock::now();          // 获取当前时间点
        std::time_t now_time_t   = std::chrono::system_clock::to_time_t(now); // 转换为time_t类型
        std::tm local_tm         = *std::localtime(&now_time_t);              // 转换为本地时间
        uint64_t dis_millseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                                   - std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;

        // 年月日时分
        auto curtime = StringUtil::Format(fmt,
                                          local_tm.tm_year + 1900,
                                          local_tm.tm_mon + 1,
                                          local_tm.tm_mday,
                                          local_tm.tm_hour,
                                          local_tm.tm_min,
                                          local_tm.tm_sec,
                                          (int)dis_millseconds);
        return curtime;
    }

    static size_t TimeSince()
    {
        return std::chrono::system_clock::now().time_since_epoch().count();
    }

    static std::string GetCurTime()
    {
        auto now               = std::chrono::system_clock::now();          // 获取当前时间点
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now); // 转换为time_t类型
        std::tm local_tm       = *std::localtime(&now_time_t);              // 转换为本地时间

        // 年月日时分
        auto curtime = StringUtil::Format("%d%02d%02d%02d%02d%02d",
                                          local_tm.tm_year + 1900,
                                          local_tm.tm_mon + 1,
                                          local_tm.tm_mday,
                                          local_tm.tm_hour,
                                          local_tm.tm_min,
                                          local_tm.tm_sec);
        return curtime;
    }
};

} // namespace util
#endif // __TIMER__