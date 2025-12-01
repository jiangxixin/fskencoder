// src/encoder.h
#pragma once
#include <string>
#include <cstdint>
#include <array>

// 16-FSK 编码参数：一个符号携带 4 bit
// 使用 DFT bin 对齐的频率：f_k = bin * Fs / N
struct EncodeParams {
    double   symbolDurationSec = 0.001;       // 符号时长（秒）
    uint32_t sampleRate        = 44100;       // 采样率
    int16_t  amplitude         = 12000;       // 正弦波幅值
    int      syncSymbols       = 64;          // 前导同步符号个数

    // 16 个 bin index，对应 4bit 值 0..15
    // 注意：所有 bin 必须满足 0 < bin < N/2
    // 对默认 Fs=44100, symdur=0.001 => N≈44，bins 最大 18 仍小于 N/2≈22
    std::array<int, 16> bins = {
        3,  4,  5,  6,
        7,  8,  9, 10,
        11, 12, 13, 14,
        15, 16, 17, 18
    };
};

bool encodeFileToWav(
    const std::string& inputBinPath,
    const std::string& outputWavPath,
    const EncodeParams& params = {}
);