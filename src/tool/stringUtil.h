#ifndef __STRING_UTIL_H__
#define __STRING_UTIL_H__

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace util {

/**
 * 常用字符串工具。
 *
 * 设计原则：
 * - 查询类接口优先使用 std::string_view，避免无意义复制；
 * - Split 返回 view 时，调用者必须保证源字符串生命周期长于结果；
 * - Find/Split 对于空输入和空分隔符均有明确行为；
 * - 带 _Self 后缀的方法原地修改字符串。
 */
class StringUtil
{
public:
    // printf 风格格式化。fmt 为空或格式化失败时返回空字符串。
    template <typename... Args>
    static std::string Format(const char* fmt, Args... args)
    {
        if (!fmt)
            return std::string();

        const int required = std::snprintf(nullptr, 0, fmt, args...);
        if (required < 0)
            return std::string();

        const size_t size = static_cast<size_t>(required);
        std::unique_ptr<char[]> buffer(new char[size + 1]);
        std::snprintf(buffer.get(), size + 1, fmt, args...);
        return std::string(buffer.get(), size);
    }

    // 大小写转换。实现中转换为 unsigned char，避免负字符传入 std::tolower 的未定义行为。
    static std::string ToLower(const std::string& str);
    static std::string ToUpper(const std::string& str);
    static std::string& ToLower_Self(std::string& str);
    static std::string& ToUpper_Self(std::string& str);

    // 字符串查询接口。
    static const char* Find(const char* src, size_t srcSize, const char dst);
    static const char* Find(const char* src, size_t srcSize, const char* dst);
    static const char* Find(const char* src, size_t srcSize, const char* dst, size_t dstSize);

    static bool StartsWith(std::string_view text, std::string_view prefix) noexcept;
    static bool EndsWith(std::string_view text, std::string_view suffix) noexcept;
    static bool Contains(std::string_view text, std::string_view token) noexcept;

    // Trim 返回原字符串的视图；TrimCopy 返回独立字符串。
    static std::string_view Trim(std::string_view value, std::string_view chars = " \t\r\n") noexcept;
    static std::string_view TrimStart(std::string_view value, std::string_view chars = " \t\r\n") noexcept;
    static std::string_view TrimEnd(std::string_view value, std::string_view chars = " \t\r\n") noexcept;
    static std::string TrimCopy(std::string_view value, std::string_view chars = " \t\r\n");

    // 原地删除首尾属于 chars 字符集合的字符。
    static std::string& Trim_Self(std::string& str, const std::string& unStr = " \t\r\n");
    static std::string& TrimStart_Self(std::string& str, const std::string& unStr);
    static std::string& TrimEnd_Self(std::string& str, const std::string& unStr);

    // Split：返回字符串副本，兼容旧调用方。
    static std::vector<std::string> Split(const std::string& srcStr, const std::string& splitStr);

    // SplitView：结果中的 view 引用原始 srcStr，不复制字符串。
    static std::vector<std::string_view> SplitView(std::string_view srcStr, std::string_view splitStr);

    // 用一个分隔符拼接字符串集合。
    static std::string Join(const std::vector<std::string>& values, const std::string& delimiter);
    static std::string Join(const std::vector<std::string_view>& values, const std::string& delimiter);

    // Replace：from 为空时返回原字符串；limit <= 0 表示替换全部匹配。
    static std::string Replace(const std::string& input,
                               const std::string& from,
                               const std::string& to,
                               int limit = 0);

    // 删除字符串中所有属于 chars 的字符。
    static std::string Remove(const std::string& str, const std::string& chars);
    static void Remove_Self(std::string& str, const std::string& chars);
};

} // namespace util
#endif
