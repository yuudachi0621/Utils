#ifndef __TIMER__
#define __TIMER__

#include "stringUtil.h"

#include <chrono>
#include <ctime>
#include <string>

namespace util {

/**
 * 日期时间工具。
 *
 * 所有格式化接口都先获取同一个时间点，再转换为本地时间；不会在多线程中
 * 直接调用非线程安全的 std::localtime。
 */
class Timer
{
public:
    static size_t Years()
    {
        std::time_t now = 0;
        const std::tm local = NowLocal(now);
        return static_cast<size_t>(local.tm_year + 1900);
    }

    static size_t Months()
    {
        std::time_t now = 0;
        const std::tm local = NowLocal(now);
        return static_cast<size_t>(local.tm_mon + 1);
    }

    static size_t Days()
    {
        std::time_t now = 0;
        const std::tm local = NowLocal(now);
        return static_cast<size_t>(local.tm_mday);
    }

    // 年-月-日。
    static std::string FormatYMD(const char* fmt = "%d-%02d-%02d")
    {
        std::time_t now = 0;
        const std::tm local = NowLocal(now);
        return StringUtil::Format(fmt, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    }

    // 时:分:秒.毫秒。
    static std::string FormatHMSS(const char* fmt = "%02d:%02d:%02d.%03d")
    {
        const auto now = std::chrono::system_clock::now();
        const std::tm local = ToLocalTime(std::chrono::system_clock::to_time_t(now));
        return StringUtil::Format(fmt,
                                  local.tm_hour,
                                  local.tm_min,
                                  local.tm_sec,
                                  Milliseconds(now));
    }

    // 年-月-日 时:分:秒。
    static std::string FormatYMDHMS(const char* fmt = "%d-%02d-%02d %02d:%02d:%02d")
    {
        std::time_t now = 0;
        const std::tm local = NowLocal(now);
        return StringUtil::Format(fmt,
                                  local.tm_year + 1900,
                                  local.tm_mon + 1,
                                  local.tm_mday,
                                  local.tm_hour,
                                  local.tm_min,
                                  local.tm_sec);
    }

    // 年-月-日 时:分:秒.毫秒。
    static std::string FormatYMDHMSS(const char* fmt = "%d-%02d-%02d %02d:%02d:%02d.%03d")
    {
        const auto now = std::chrono::system_clock::now();
        const std::tm local = ToLocalTime(std::chrono::system_clock::to_time_t(now));
        return StringUtil::Format(fmt,
                                  local.tm_year + 1900,
                                  local.tm_mon + 1,
                                  local.tm_mday,
                                  local.tm_hour,
                                  local.tm_min,
                                  local.tm_sec,
                                  Milliseconds(now));
    }

    // 返回当前系统时钟的原始计数，适合做粗粒度时间戳比较。
    static size_t TimeSince()
    {
        return static_cast<size_t>(std::chrono::system_clock::now().time_since_epoch().count());
    }

    // 返回 yyyyMMddHHmmss 格式的当前本地时间。
    static std::string GetCurTime()
    {
        return FormatYMDHMS("%d%02d%02d%02d%02d%02d");
    }

private:
    // 将 system_clock 时间点转换为本地时间副本，避免共享静态 tm。
    static std::tm ToLocalTime(std::time_t time)
    {
        std::tm local{};
#ifdef _MSC_VER
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        return local;
    }

    static std::tm NowLocal(std::time_t& nowTime)
    {
        const auto now = std::chrono::system_clock::now();
        nowTime = std::chrono::system_clock::to_time_t(now);
        return ToLocalTime(nowTime);
    }

    // 提取当前时间点在一个完整秒内的毫秒部分。
    template <typename TimePoint>
    static int Milliseconds(const TimePoint& now)
    {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        int result = static_cast<int>(millis % 1000);
        if (result < 0)
            result += 1000;
        return result;
    }
};

} // namespace util
#endif
