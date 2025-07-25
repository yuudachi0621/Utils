#include "FileUtil.h"
#include "filesystem.hpp"

bool util::FileUtil::CreateFolder(const std::string& folderPath)
{
    try
    {
        if (ghc::filesystem::exists(folderPath) && ghc::filesystem::is_directory(folderPath)) return true;
        return ghc::filesystem::create_directories(folderPath);
    } catch (const std::exception&)
    {
        return false;
    }
}
