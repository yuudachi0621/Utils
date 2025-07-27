#include "VariableBuffer.h"

VariableBuffer::VariableBuffer(int initBufferSize)
    : m_buffer(initBufferSize), m_readPos(0), m_writePos(0)
{
}

VariableBuffer::~VariableBuffer()
{
    std::vector<char> empty;
    std::swap(m_buffer, empty);
    m_readPos  = 0;
    m_writePos = 0;
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
    return BeginPtr() + m_readPos;
}

std::string VariableBuffer::GetValidDataToStr() const
{
    std::string result(GetValidData(), ValidLength());
    return result;
}

void VariableBuffer::Append(const char* str, size_t len)
{
    EnsureWriteable(len);
    std::copy(str, str + len, WritableBegin());
    AddWritePos(len);
}

void VariableBuffer::AddReadPos(size_t len)
{
    m_readPos += len;
}

void VariableBuffer::AddWritePos(size_t len)
{
    m_writePos += len;
}

void VariableBuffer::Reset()
{
    memset(BeginPtr(), 0, m_buffer.size());
    m_readPos  = 0;
    m_writePos = 0;
}

char* VariableBuffer::BeginPtr()
{
    return &*m_buffer.begin();
}

const char* VariableBuffer::BeginPtr() const
{
    return &*m_buffer.begin();
}

char* VariableBuffer::ReadableBegin()
{
    return BeginPtr() + m_readPos;
}

char* VariableBuffer::WritableBegin()
{
    return BeginPtr() + m_writePos;
}

void VariableBuffer::EnsureWriteable(size_t len)
{
    if (WritableLength() < len)
    {
        ResizeSpace(len);
    }
}

void VariableBuffer::ResizeSpace(size_t len)
{
    if (WritableLength() + InvalidLength() < len)
    {
        m_buffer.resize(m_writePos + len + 1);
    }
    else
    {
        size_t validLen = ValidLength();
        std::copy(BeginPtr() + m_readPos, BeginPtr() + m_writePos, BeginPtr());
        m_readPos  = 0;
        m_writePos = validLen;
    }
}