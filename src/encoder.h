#pragma once
#include <string>
#include <cstdint>
#include <array>

// 16-FSK 编码参数：一个符号携带 4 bit
struct EncodeParams {
    double   symbolDurationSec = 0.001;       // 每个符号时长（秒）
    uint32_t sampleRate        = 44100;       // 采样率
    int16_t  amplitude         = 12000;       // 正弦波幅值
    int      syncSymbols       = 64;          // 前导同步符号个数

    // 16 个频率，对应 4bit 值 0..15
    std::array<double, 16> freqs = {
        2000.0, 2300.0, 2600.0, 2900.0,
        3200.0, 3500.0, 3800.0, 4100.0,
        4400.0, 4700.0, 5000.0, 5300.0,
        5600.0, 5900.0, 6200.0, 6500.0
    };
};

bool encodeFileToWav(
    const std::string& inputBinPath,
    const std::string& outputWavPath,
    const EncodeParams& params = {}
);