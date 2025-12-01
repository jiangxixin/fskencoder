#pragma once
#include <vector>
#include <cstdint>

// 把字节展开为 bit 向量（高位在前，元素为 0/1）
void bytesToBits(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& bits);

// 把 bit 向量打包成字节（高位在前），不足 8 位的最后一字节低位补 0
void bitsToBytes(const std::vector<uint8_t>& bits, std::vector<uint8_t>& bytes);

// 卷积编码：rate 1/2, K=3, G1=7(oct)=111b, G2=5(oct)=101b
// inBits: 0/1
// outBits: 0/1，长度约为 2 * (inBits.size() + (K-1))
void convEncode(const std::vector<uint8_t>& inBits, std::vector<uint8_t>& outBits);

// 卷积 Viterbi 解码（硬判决定距，已知编码时添加了 K-1 个尾比特让状态回到 0）
// inBits: 0/1，长度为偶数
// outBits: 0/1，输出原始信息比特
bool convDecode(const std::vector<uint8_t>& inBits, std::vector<uint8_t>& outBits);