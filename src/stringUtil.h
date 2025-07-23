#ifndef __STRING_UTIL_H__
#define __STRING_UTIL_H__

#include <algorithm> // 包含 std::tolower
#include <memory>
#include <string>
#include <vector>

namespace util {
class StringUtil
{
public:
    template <typename... Args>
    static std::string Format(const char* fmt, Args... args)
    {
        size_t size = std::snprintf(nullptr, 0, fmt, args...);
        if (size <= 0)
            return std::string();

        std::unique_ptr<char[]> buf = std::unique_ptr<char[]>(new char[size + 1]());
        std::snprintf(buf.get(), size + 1, fmt, args...);
        return std::string(buf.get(), size);
    }

    // 转换大/小写
    static std::string ToLower(const std::string& str);
    static std::string ToUpper(const std::string& str);

    static const char* Find(const char* src, size_t srcSize, const char dst);
    static const char* Find(const char* src, size_t srcSize, const char* dst, size_t dstSize);

    static std::vector<std::string> Split(const std::string& srcStr, const std::string& splitStr);
    static std::vector<std::string_view> Split(const std::string_view& srcStr, const std::string& splitStr);

    static std::string Replace(const std::string& oldStr, const std::string& newStr, int limit = 0);

    static std::string& Trim_Self(std::string& str, const std::string& unStr)
    {
        TrimStart_Self(str, unStr);
        TrimEnd_Self(str, unStr);
        return str;
    }

    static std::string& TrimStart_Self(std::string& str, const std::string& unStr)
    {
        if (unStr.empty() || str.empty()) return str;

        if (str.compare(0, unStr.size(), unStr) == 0)
        {
            str.erase(0, unStr.size());
        }
        return str;
    }

    static std::string& TrimEnd_Self(std::string& str, const std::string& unStr)
    {
        if (unStr.empty() || str.empty()) return str;

        if (str.size() >= unStr.size() && str.compare(str.size() - unStr.size(), unStr.size(), unStr) == 0)
        {
            str.erase(str.size() - unStr.size());
        }
        return str;
    }

    // static std::string Remove(const std::string& str);
    // static std::string RightRemove(const std::string& str);
    static void Remove_Self(std::string& str, const std::string& unStr);
    static void RightRemove_Self(std::string& str);
};
} // namespace util
#endif