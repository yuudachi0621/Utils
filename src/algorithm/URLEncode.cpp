#include "URLEncode.h"
#include <sstream>
#include <iomanip>

std::string URLEncode::encode(const std::string& value)
{
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : value)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded << c;
        }
        else
        {
            encoded << '%' << std::setw(2) << std::uppercase << (int)c;
        }
    }
    return encoded.str();
}

std::string URLEncode::decode(const std::string& value)
{
    std::string decoded;
    std::istringstream ss(value);
    char c;

    while (ss >> c)
    {
        if (c == '%')
        {
            int hexValue;
            char hexChars[3] = {0};
            ss.get(hexChars, 3);
            hexChars[2] = '\0';
            hexValue    = std::strtol(hexChars, nullptr, 16);
            decoded += static_cast<char>(hexValue);
        }
        else if (c == '+')
        {
            decoded += ' ';
        }
        else
        {
            decoded += c;
        }
    }
    return decoded;
}
