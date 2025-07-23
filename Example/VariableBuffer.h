#pragma once
#include <atomic>
#include <string>
#include <vector>
class VariableBuffer
{
public:
    VariableBuffer(int initBufferSize = 1024);
    ~VariableBuffer() = default;

    // 过期数据长度
    size_t InvalidLength() const;

    // 有效数据长度
    size_t ValidLength() const;

    // 有效剩余写入长度
    size_t WritableLength() const;

    // 返回有效数据
    const char* GetValidData() const;
    std::string GetValidDataToStr() const;

    // 添加数据
    void Append(const char* str, size_t len);

    // 偏移
    void AddReadPos(size_t len);
    void AddWritePos(size_t len);

    void Reset();

private:
    char* BeginPtr();

    const char* BeginPtr() const;

    // 有效可读地址
    char* ReadableBegin();

    // 有效可写地址
    char* WritableBegin();

    // 确保写入长度有效
    void EnsureWriteable(size_t len);

    // 调整空间
    void ResizeSpace(size_t len);

private:
    std::vector<char> m_buffer;
    std::atomic<size_t> m_readPos;
    std::atomic<size_t> m_writePos;
};
