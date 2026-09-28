#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

/**
 * 可变长度字节缓冲区。
 *
 * 设计说明：
 * - 适用于单线程生产者/消费者模式，内部不做线程同步；
 * - readPos/writePos 使用普通 size_t，避免原子变量造成“线程安全”的误解；
 * - Reset 只重置读写位置，不释放已分配容量；
 * - 所有消费/写入长度都会被钳制到当前有效范围内，避免位置越过边界。
 */
class VariableBuffer
{
public:
    explicit VariableBuffer(size_t initBufferSize = 1024);
    ~VariableBuffer() = default;

    VariableBuffer(const VariableBuffer&) = delete;
    VariableBuffer& operator=(const VariableBuffer&) = delete;

    // 已读取并失效的数据长度。
    size_t InvalidLength() const;

    // 当前可读有效数据长度。
    size_t ValidLength() const;

    // 当前可写剩余长度。
    size_t WritableLength() const;

    // 返回有效数据起始地址。
    const char* GetValidData() const;
    char* ReadableBegin();
    char* WritableBegin();

    // 复制并追加数据；空间不足时自动扩容或整理。
    void Append(const char* str, size_t len);

    // 返回有效数据副本。
    std::string GetValidDataToStr() const;

    // 移动读指针；超过 ValidLength 时自动截断到有效范围。
    void AddReadPos(size_t len);
    void Consume(size_t n);

    // 移动写指针；超过 WritableLength 时自动截断到可写范围。
    void AddWritePos(size_t len);

    // 重置逻辑位置，保留底层容量。
    void Reset();

private:
    // 确保底层容量至少还能写入 len 字节。
    void EnsureWritable(size_t len);

    // 扩容或将未读数据移动到头部。
    void ResizeSpace(size_t len);

private:
    std::vector<char> m_buffer;
    size_t m_readPos = 0;
    size_t m_writePos = 0;
};
