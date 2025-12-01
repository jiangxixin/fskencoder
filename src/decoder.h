#pragma once
#include <string>
#include <cstdint>
#include <array>

// 16-FSK 解码参数（需与编码端保持一致）
struct DecodeParams {
    double   symbolDurationSec = 0.001;
    uint32_t sampleRate        = 44100;
    int      syncSymbols       = 64;
    std::array<double, 16> freqs = {
        2000.0, 2300.0, 2600.0, 2900.0,
        3200.0, 3500.0, 3800.0, 4100.0,
        4400.0, 4700.0, 5000.0, 5300.0,
        5600.0, 5900.0, 6200.0, 6500.0
    };
};

bool decodeWavToFile(
    const std::string& inputWavPath,
    const std::string& outputBinPath,
    const DecodeParams& params = {}
);