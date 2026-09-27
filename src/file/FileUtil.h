#ifndef __UTILS_FILE_H__
#define __UTILS_FILE_H__

#include <string>

namespace util {

class FileUtil
{
public:
    // 读取文件
    static std::string ReadFile(const std::string& filename);
    // 写文件
    static void WriteFile(const std::string& filename, const char* data, size_t size);
    // 重命名文件
    static bool RenameFile(const std::string& oldName, const std::string& newName);

    // 路径是否存在
    static bool IsPathExist(const std::string& path);
    // 文件是否存在
    static bool IsFileExist(const std::string& file);
    // 文件夹是否存在
    static bool IsFolderExist(const std::string& dir);

    // 创建文件夹
    static bool CreateFolder(const std::string& folder);
};

} // namespace util
#endif // __UTILS_FILE_H__