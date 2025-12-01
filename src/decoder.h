// src/decoder.h
#pragma once
#include <string>
#include <cstdint>
#include <array>

// 16-FSK 解码参数（bin 配置需与编码端保持一致）
struct DecodeParams {
    double   symbolDurationSec = 0.001;
    uint32_t sampleRate        = 44100;
    int      syncSymbols       = 64;

    std::array<int, 16> bins = {
        3,  4,  5,  6,
        7,  8,  9, 10,
        11, 12, 13, 14,
        15, 16, 17, 18
    };
};

bool decodeWavToFile(
    const std::string& inputWavPath,
    const std::string& outputBinPath,
    const DecodeParams& params = {}
);