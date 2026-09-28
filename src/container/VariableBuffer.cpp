#include "VariableBuffer.h"

#include <cstring>
#include <utility>

VariableBuffer::VariableBuffer(size_t initBufferSize)
    : m_buffer(std::max<size_t>(initBufferSize, 1))
{
}

size_t VariableBuffer::InvalidLength() const
{
    return m_readPos;
}

size_t VariableBuffer::ValidLength() const
{
    return m_writePos - m_readPos;
}

size_t VariableBuffer::WritableLength() const
{
    return m_buffer.size() - m_writePos;
}

const char* VariableBuffer::GetValidData() const
{
    return m_buffer.data() + m_readPos;
}

char* VariableBuffer::ReadableBegin()
{
    return m_buffer.data() + m_readPos;
}

char* VariableBuffer::WritableBegin()
{
    EnsureWritable(1);
    return m_buffer.data() + m_writePos;
}

std::string VariableBuffer::GetValidDataToStr() const
{
    const size_t length = ValidLength();
    return length == 0 ? std::string() : std::string(GetValidData(), length);
}

void VariableBuffer::Append(const char* str, size_t len)
{
    if (!str || len == 0)
        return;

    EnsureWritable(len);
    std::copy_n(str, len, m_buffer.data() + m_writePos);
    AddWritePos(len);
}

void VariableBuffer::AddReadPos(size_t len)
{
    const size_t consumed = (std::min)(len, ValidLength());
    m_readPos += consumed;

    if (m_readPos == m_writePos)
    {
        m_readPos = 0;
        m_writePos = 0;
    }
}

void VariableBuffer::Consume(size_t n)
{
    AddReadPos(n);
}

void VariableBuffer::AddWritePos(size_t len)
{
    m_writePos += (std::min)(len, WritableLength());
}

void VariableBuffer::Reset()
{
    m_readPos = 0;
    m_writePos = 0;
}

void VariableBuffer::EnsureWritable(size_t len)
{
    if (WritableLength() < len)
        ResizeSpace(len);
}

void VariableBuffer::ResizeSpace(size_t len)
{
    const size_t validLength = ValidLength();

    // 尾部空间加已失效空间足够时，将未读数据移动到缓冲区头部。
    if (m_buffer.size() - m_writePos + m_readPos >= len)
    {
        if (validLength != 0 && m_readPos != 0)
            std::memmove(m_buffer.data(), m_buffer.data() + m_readPos, validLength);

        m_readPos = 0;
        m_writePos = validLength;
        return;
    }

    // 即使整理后也不足，才进行扩容；先保留未读数据。
    const size_t newSize = m_writePos + len;
    m_buffer.resize(newSize);
}
