#include "FileUtil.h"
#include "filesystem.hpp"

std::string util::FileUtil::ReadFile(const std::string& filename)
{
    FILE* fp = nullptr;
    fp       = fopen(filename.data(), "rb");
    if (!fp)
        return std::string();

    fseek(fp, 0, SEEK_END);
    size_t fileSize = ftell(fp);
    rewind(fp);

    std::unique_ptr<char[]> buf = std::unique_ptr<char[]>(new char[fileSize + 1]());

    size_t bytesRead = fread(buf.get(), 1, fileSize, fp);
    fclose(fp);

    if (bytesRead != fileSize)
        return std::string();
    return std::string(buf.get(), fileSize);
}

void util::FileUtil::WriteFile(const std::string& filename, const char* data, size_t size)
{
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs.is_open()) return;
    ofs.write(data, size);
}

bool util::FileUtil::RenameFile(const std::string& oldName, const std::string& newName)
{
    if (!IsFileExist(oldName))
        return false;
    try
    {
        ghc::filesystem::rename(ghc::filesystem::path(oldName), ghc::filesystem::path(newName));
        return true;
    } catch (const std::exception&)
    {
        return false;
    }
}

bool util::FileUtil::IsPathExist(const std::string& path)
{
    return ghc::filesystem::exists(path);
}

bool util::FileUtil::IsFileExist(const std::string& file)
{
    if (ghc::filesystem::exists(file))
    {
        if (ghc::filesystem::is_regular_file(file))
        {
            return true;
        }
    }
    return false;
}

bool util::FileUtil::IsFolderExist(const std::string& dir)
{
    if (ghc::filesystem::exists(dir))
    {
        if (ghc::filesystem::is_directory(dir))
        {
            return true;
        }
    }
    return false;
}

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
