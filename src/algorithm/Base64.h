#pragma once
#include <string>
#include <vector>

const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

class Base64
{
public:
    static std::string encode(const std::vector<uint8_t>& data);

    static std::string encode(const std::string& str);

    static std::vector<uint8_t> decode(const std::string& encoded_string);

    static std::string decodeToString(const std::string& encoded_string);

private:
    static inline bool is_base64(uint8_t c)
    {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }
};
