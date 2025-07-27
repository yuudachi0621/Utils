#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <string>

class MMapFile
{
    MMapFile(const MMapFile&)            = delete;
    MMapFile& operator=(const MMapFile&) = delete;

public:
    MMapFile()
        : m_data(nullptr), m_size(0)
    {
#ifdef _WIN32
        m_mappingHandle = NULL;
        m_fileHandle    = NULL;
#else
        m_fd = 0;
#endif
    }

    MMapFile(const std::string& filePath)
        : m_data(nullptr), m_size(0)
    {
        Init(filePath);
    }

    bool Init(const std::string& filePath)
    {
        if (nullptr != m_data)
            unmap();

#ifdef _WIN32
        // Windows实现
        m_fileHandle = CreateFileA(
            filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (m_fileHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        // 获取文件大小
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(m_fileHandle, &fileSize))
        {
            CloseHandle(m_fileHandle);
            return false;
        }
        m_size = static_cast<size_t>(fileSize.QuadPart);

        // 创建文件映射
        m_mappingHandle = CreateFileMapping(
            m_fileHandle,
            NULL,
            PAGE_READONLY,
            0,
            0,
            NULL);

        if (m_mappingHandle == NULL)
        {
            CloseHandle(m_fileHandle);
            return false;
        }

        // 映射视图
        m_data = MapViewOfFile(
            m_mappingHandle,
            FILE_MAP_READ,
            0,
            0,
            m_size);

        if (nullptr == m_data)
        {
            CloseHandle(m_mappingHandle);
            CloseHandle(m_fileHandle);
            return false;
        }
#else
        m_fd = open(filePath.c_str(), O_RDONLY);
        if (m_fd == -1)
        {
            return false;
        }

        // 获取文件大小
        struct stat sb;
        if (fstat(m_fd, &sb) == -1)
        {
            close(m_fd);
            return false;
        }
        m_size = sb.st_size;

        // 内存映射
        m_data = mmap(NULL, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
        if (data_ == MAP_FAILED)
        {
            close(m_fd);
            return false;
        }

        // 建议内核预读
        madvise(m_data, m_size, MADV_SEQUENTIAL);
#endif
        return true;
    }

    ~MMapFile()
    {
        unmap();
    }

    // 获取映射数据指针
    const char* data() const noexcept
    {
        return static_cast<const char*>(m_data);
    }

    // 获取文件大小
    size_t size() const noexcept
    {
        return m_size;
    }

    // 是否映射成功
    bool is_mapped() const noexcept
    {
        return m_data != nullptr;
    }

    void unmap()
    {
        if (nullptr != m_data)
        {
#ifdef _WIN32
            UnmapViewOfFile(m_data);
            CloseHandle(m_mappingHandle);
            CloseHandle(m_fileHandle);
#else
            munmap(m_data, m_size);
            close(m_fd);
#endif
            m_data = nullptr;
            m_size = 0;
        }
    }

private:
#ifdef _WIN32
    HANDLE m_fileHandle;
    HANDLE m_mappingHandle;
#else
    int m_fd;
#endif
    void* m_data;
    size_t m_size;
};