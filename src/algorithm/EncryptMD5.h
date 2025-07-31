#pragma once
#include <string>

class EncryptMD5
{
public:
    EncryptMD5()
    {
        reset();
    }

    // 计算字符串的MD5值
    std::string compute(const std::string& input);

private:
    // 内部处理函数
    void transform(const uint8_t block[64]);

    // 填充输入数据
    void update(const uint8_t* input, size_t length);

    // 完成计算并返回结果
    void final(uint8_t digest[16]);

    // 重置MD5计算状态
    void reset();

private:
    uint32_t state[4];  // MD5的四个状态寄存器(A, B, C, D)
    uint8_t buffer[64]; // 512位的输入缓冲区
    uint64_t count[2];  // 64位的位计数器
};
