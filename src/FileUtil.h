#ifndef __UTILS_FILE_H__
#define __UTILS_FILE_H__

#include <string>

namespace util {

class FileUtil
{
public:
    // 创建文件夹
    static bool CreateFolder(const std::string& folder);
};

} // namespace util
#endif // __UTILS_FILE_H__