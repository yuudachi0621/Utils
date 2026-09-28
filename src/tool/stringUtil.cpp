#include "stringUtil.h"

#include <cstring>
#include <algorithm>

namespace {
void ToLowerImpl(std::string& value)
{
    for (char& c : value)
    {
        const unsigned char uch = static_cast<unsigned char>(c);
        c                       = static_cast<char>(std::tolower(uch));
    }
}

void ToUpperImpl(std::string& value)
{
    for (char& c : value)
    {
        const unsigned char uch = static_cast<unsigned char>(c);
        c                       = static_cast<char>(std::toupper(uch));
    }
}
} // namespace

std::string util::StringUtil::ToLower(const std::string& str)
{
    std::string result(str);
    return ToLower_Self(result);
}

std::string util::StringUtil::ToUpper(const std::string& str)
{
    std::string result(str);
    return ToUpper_Self(result);
}

std::string& util::StringUtil::ToLower_Self(std::string& str)
{
    ToLowerImpl(str);
    return str;
}

std::string& util::StringUtil::ToUpper_Self(std::string& str)
{
    ToUpperImpl(str);
    return str;
}

const char* util::StringUtil::Find(const char* src, size_t srcSize, const char dst)
{
    if (!src || srcSize == 0)
        return nullptr;

    return static_cast<const char*>(std::memchr(src, static_cast<unsigned char>(dst), srcSize));
}

const char* util::StringUtil::Find(const char* src, size_t srcSize, const char* dst)
{
    return dst ? Find(src, srcSize, dst, std::strlen(dst)) : nullptr;
}

const char* util::StringUtil::Find(const char* src, size_t srcSize, const char* dst, size_t dstSize)
{
    if (!src || !dst)
        return nullptr;

    // 空模式按字符串查找惯例返回起始位置。
    if (dstSize == 0)
        return src;

    if (srcSize < dstSize)
        return nullptr;

    const char* end = src + srcSize;
    const char* it  = std::search(src, end, dst, dst + dstSize);
    return it == end ? nullptr : it;
}

bool util::StringUtil::StartsWith(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool util::StringUtil::EndsWith(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool util::StringUtil::Contains(std::string_view text, std::string_view token) noexcept
{
    return text.find(token) != std::string_view::npos;
}

std::string_view util::StringUtil::TrimStart(std::string_view value, std::string_view chars) noexcept
{
    while (!value.empty() && chars.find(value.front()) != std::string_view::npos)
        value.remove_prefix(1);
    return value;
}

std::string_view util::StringUtil::TrimEnd(std::string_view value, std::string_view chars) noexcept
{
    while (!value.empty() && chars.find(value.back()) != std::string_view::npos)
        value.remove_suffix(1);
    return value;
}

std::string_view util::StringUtil::Trim(std::string_view value, std::string_view chars) noexcept
{
    return TrimEnd(TrimStart(value, chars), chars);
}

std::string util::StringUtil::TrimCopy(std::string_view value, std::string_view chars)
{
    return std::string(Trim(value, chars));
}

std::string& util::StringUtil::Trim_Self(std::string& str, const std::string& unStr)
{
    TrimStart_Self(str, unStr);
    return TrimEnd_Self(str, unStr);
}

std::string& util::StringUtil::TrimStart_Self(std::string& str, const std::string& unStr)
{
    if (str.empty() || unStr.empty())
        return str;

    const size_t pos = str.find_first_not_of(unStr);
    if (pos == std::string::npos)
        str.clear();
    else if (pos > 0)
        str.erase(0, pos);
    return str;
}

std::string& util::StringUtil::TrimEnd_Self(std::string& str, const std::string& unStr)
{
    if (str.empty() || unStr.empty())
        return str;

    const size_t pos = str.find_last_not_of(unStr);
    if (pos == std::string::npos)
        str.clear();
    else
        str.erase(pos + 1);
    return str;
}

std::vector<std::string_view> util::StringUtil::SplitView(std::string_view srcStr, std::string_view splitStr)
{
    if (splitStr.empty())
        return {srcStr};

    std::vector<std::string_view> result;
    size_t start = 0;

    while (true)
    {
        const size_t pos = srcStr.find(splitStr, start);
        if (pos == std::string_view::npos)
        {
            result.emplace_back(srcStr.substr(start));
            break;
        }

        result.emplace_back(srcStr.substr(start, pos - start));
        start = pos + splitStr.size();
    }

    return result;
}

std::vector<std::string> util::StringUtil::Split(const std::string& srcStr, const std::string& splitStr)
{
    const std::vector<std::string_view> views = SplitView(std::string_view(srcStr), std::string_view(splitStr));

    std::vector<std::string> result;
    result.reserve(views.size());
    for (const std::string_view view : views)
        result.emplace_back(view);

    return result;
}

std::string util::StringUtil::Join(const std::vector<std::string>& values, const std::string& delimiter)
{
    if (values.empty())
        return std::string();

    size_t totalSize = delimiter.size() * (values.size() - 1);
    for (const std::string& value : values)
        totalSize += value.size();

    std::string result;
    result.reserve(totalSize);
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            result += delimiter;
        result += values[i];
    }
    return result;
}

std::string util::StringUtil::Join(const std::vector<std::string_view>& values, const std::string& delimiter)
{
    if (values.empty())
        return std::string();

    size_t totalSize = delimiter.size() * (values.size() - 1);
    for (const std::string_view value : values)
        totalSize += value.size();

    std::string result;
    result.reserve(totalSize);
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            result += delimiter;
        result.append(values[i]);
    }
    return result;
}

std::string util::StringUtil::Replace(const std::string& input,
                                      const std::string& from,
                                      const std::string& to,
                                      int limit)
{
    if (from.empty())
        return input;

    std::string result;
    result.reserve(input.size());

    size_t start     = 0;
    int replaced     = 0;
    bool replacedAny = false;

    while (start <= input.size())
    {
        const size_t pos = input.find(from, start);
        if (pos == std::string::npos)
        {
            result.append(input, start, std::string::npos);
            break;
        }

        result.append(input, start, pos - start);
        result += to;
        start = pos + from.size();
        ++replaced;
        replacedAny = true;

        if (limit > 0 && replaced >= limit)
        {
            result.append(input, start, std::string::npos);
            break;
        }
    }

    return replacedAny ? result : input;
}

std::string util::StringUtil::Remove(const std::string& str, const std::string& chars)
{
    std::string result(str);
    Remove_Self(result, chars);
    return result;
}

void util::StringUtil::Remove_Self(std::string& str, const std::string& chars)
{
    if (str.empty() || chars.empty())
        return;

    str.erase(std::remove_if(str.begin(), str.end(), [&chars](char c) {
                  return chars.find(c) != std::string::npos;
              }),
              str.end());
}
