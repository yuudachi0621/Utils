#pragma once
#include <string>
class URLEncode
{
public:
    // URL编码
    static std::string encode(const std::string& value);

    // URL解码
    static std::string decode(const std::string& value);
};
