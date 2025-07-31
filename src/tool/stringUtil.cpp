#include "stringUtil.h"
#include <list>

std::string util::StringUtil::ToLower(const std::string& str)
{
    std::string result;
    result.resize(str.size());
    for (size_t i = 0; i < str.size(); i++)
    {
        result[i] = static_cast<char>(std::tolower(str[i]));
    }
    return result;
}

std::string util::StringUtil::ToUpper(const std::string& str)
{
    std::string result;
    result.resize(str.size());
    for (size_t i = 0; i < str.size(); i++)
    {
        result[i] = static_cast<char>(std::toupper(str[i]));
    }
    return result;
}

const char* util::StringUtil::Find(const char* src, size_t srcSize, const char dst)
{
    for (int index = 0; index < srcSize; index++)
    {
        if (dst == *(src + index))
        {
            return src + index;
        }
    }
    return nullptr;
}

const char* util::StringUtil::Find(const char* src, size_t srcSize, const char* dst, size_t dstSize)
{
    if (srcSize == 0)
        return nullptr;

    // init kmp next
    std::vector<int> next;
    next.resize(dstSize, 0);
    int tempindex = -1;
    next[0]       = tempindex;
    for (int i = 1; i < dstSize; i++)
    {
        while (tempindex >= 0 && dst[i] != dst[tempindex + 1])
        {
            tempindex = next[tempindex];
        }
        if (dst[i] == dst[tempindex + 1])
        {
            tempindex++;
        }
        next[i] = tempindex;
    }
    // init kmp end

    int j = -1;
    for (int i = 0; i < srcSize; i++)
    {
        while (j >= 0 && src[i] != dst[j + 1])
        {
            j = next[j];
        }
        if (src[i] == dst[j + 1])
        {
            j++;
        }
        if (j == (dstSize - 1))
        {
            return src + (i - dstSize + 1);
        }
    }
    return nullptr;
}

std::vector<std::string> util::StringUtil::Split(const std::string& srcStr, const std::string& splitStr)
{
    std::vector<std::string> result;

    const char* src = srcStr.data();
    const char* dst = splitStr.data();
    size_t srcLen   = srcStr.size();
    size_t dstLen   = splitStr.size();

    if (!src || !dst) return result;

    const char* srcTemp = src;
    size_t srcTempLen   = srcLen;
    const char* dstTemp = nullptr;

    std::list<int> dstList;
    while (nullptr != (dstTemp = StringUtil::Find(srcTemp, srcTempLen, dst, dstLen)))
    {
        size_t offset = dstTemp - src;
        dstList.push_back(offset);
        srcTemp    = dstTemp + dstLen;
        srcTempLen = srcLen - offset - dstLen;
    }

    if (dstList.size() == 0)
    {
        result.push_back(srcStr.substr(0));
        return result;
    }

    size_t len  = srcLen;
    size_t len1 = 0;
    size_t len2 = 0;

    for (auto index = dstList.begin(); index != dstList.end(); index++)
    {
        len2             = len - *index;
        size_t subStrlen = len - len2 - len1;
        result.push_back(srcStr.substr(len1, subStrlen));
        len1 = len1 + subStrlen + dstLen;
    }
    size_t subStrlen = len - len1;
    result.push_back(srcStr.substr(len1, subStrlen));
    return result;
}