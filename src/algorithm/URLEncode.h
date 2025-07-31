#pragma once
#include <string>
class URLEncode
{
public:
    // URL±àÂë
    static std::string encode(const std::string& value);

    // URL½âÂë
    static std::string decode(const std::string& value);
};
