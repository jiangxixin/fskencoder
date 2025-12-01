// src/decoder.cpp
#include "decoder.h"
#include "wav_io.h"
#include "fec.h"
#include "frame.h"

#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string>
#include <array>
#include <algorithm> // for std::min

namespace {

constexpr float PI_F = 3.14159265358979323846f;

struct SymbolShape {
    uint32_t N; // 每个符号的采样点数
};

SymbolShape computeSymbolShape(uint32_t sampleRate, double symbolDurationSec) {
    uint32_t N = static_cast<uint32_t>(sampleRate * symbolDurationSec);
    if (N == 0) {
        throw std::runtime_error("symbolDurationSec too small for given sampleRate");
    }
    return { N };
}

// Goertzel 配置（DFT bin 对齐版本）
struct GoertzelConfig {
    float coeff;   // 2*cos(omega)
    uint32_t N;    // 窗口长度
};

// 使用 bin index 直接计算 ω = 2π * bin / N
GoertzelConfig makeGoertzelConfigFromBin(
    int binIndex,
    uint32_t N
) {
    if (binIndex <= 0 || binIndex >= static_cast<int>(N / 2)) {
        throw std::runtime_error("Invalid bin index for Goertzel (must be in (0, N/2))");
    }

    GoertzelConfig cfg{};
    cfg.N = N;

    float omega = 2.0f * PI_F *
                  static_cast<float>(binIndex) /
                  static_cast<float>(N);

    cfg.coeff = 2.0f * std::cos(omega);
    return cfg;
}

float goertzelPower(
    const int16_t* data,
    const GoertzelConfig& cfg
) {
    float s_prev  = 0.0f;
    float s_prev2 = 0.0f;
    const uint32_t N = cfg.N;

    for (uint32_t i = 0; i < N; ++i) {
        float x = static_cast<float>(data[i]);
        float s = x + cfg.coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev  = s;
    }

    float power = s_prev2 * s_prev2 + s_prev * s_prev - cfg.coeff * s_prev * s_prev2;
    return power;
}

// 简单预处理：去 DC + Hann 窗
void preprocessFrame(std::vector<int16_t>& frame) {
    const uint32_t N = static_cast<uint32_t>(frame.size());
    if (N == 0) return;

    // 去直流
    float mean = 0.0f;
    for (uint32_t i = 0; i < N; ++i) {
        mean += static_cast<float>(frame[i]);
    }
    mean /= static_cast<float>(N);

    for (uint32_t i = 0; i < N; ++i) {
        float x = static_cast<float>(frame[i]) - mean;

        // Hann 窗
        float w = 0.5f - 0.5f * std::cos(2.0f * PI_F * static_cast<float>(i) /
                                         static_cast<float>(N - 1));
        x *= w;

        // 写回 int16（幅度缩放对 Goertzel 判 argmax 不重要）
        if (x > 32767.0f) x = 32767.0f;
        if (x < -32768.0f) x = -32768.0f;
        frame[i] = static_cast<int16_t>(std::lrint(x));
    }
}

// 对一个符号窗口，判决其对应的 16-FSK 频率 index (0..15)
int detectSymbolIndex(
    const int16_t* frame,
    const std::array<GoertzelConfig, 16>& cfgs
) {
    float bestPower = -1.0f;
    int bestIdx = 0;
    for (int i = 0; i < 16; ++i) {
        float p = goertzelPower(frame, cfgs[i]);
        if (p > bestPower) {
            bestPower = p;
            bestIdx = i;
        }
    }
    return bestIdx;
}

} // namespace

bool decodeWavToFile(
    const std::string& inputWavPath,
    const std::string& outputBinPath,
    const DecodeParams& params
) {
    std::ifstream ifs(inputWavPath, std::ios::binary);
    if (!ifs) {
        std::cerr << "Failed to open WAV for reading: " << inputWavPath << "\n";
        return false;
    }

    // 1. 读 WAV 头
    WavHeader header{};
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!ifs) {
        std::cerr << "Failed to read WAV header\n";
        return false;
    }

    if (std::string(header.riff, 4) != "RIFF" ||
        std::string(header.wave, 4) != "WAVE" ||
        std::string(header.fmt, 4)  != "fmt " ||
        std::string(header.data, 4) != "data") {
        std::cerr << "Invalid WAV format\n";
        return false;
    }

    if (header.audioFormat != 1 || header.numChannels != 1 || header.bitsPerSample != 16) {
        std::cerr << "Only PCM mono 16-bit supported\n";
        return false;
    }

    if (header.sampleRate != params.sampleRate) {
        std::cerr << "Sample rate mismatch. Expected " << params.sampleRate
                  << ", got " << header.sampleRate << "\n";
        return false;
    }

    // 2. 符号形状
    SymbolShape shape;
    try {
        shape = computeSymbolShape(params.sampleRate, params.symbolDurationSec);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }

    uint64_t numSamples = header.subchunk2Size / (header.bitsPerSample / 8);
    if (shape.N == 0) {
        std::cerr << "Invalid symbol shape.\n";
        return false;
    }

    uint64_t totalSymbols = numSamples / shape.N;
    if (totalSymbols <= static_cast<uint64_t>(params.syncSymbols)) {
        std::cerr << "Not enough symbols for sync and data.\n";
        return false;
    }

    // 3. 预计算 16 个 bin 的 Goertzel 配置
    std::array<GoertzelConfig, 16> cfgs;
    try {
        for (int i = 0; i < 16; ++i) {
            cfgs[i] = makeGoertzelConfigFromBin(params.bins[i], shape.N);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in makeGoertzelConfigFromBin: " << e.what() << "\n";
        return false;
    }

    std::vector<int16_t> frame(shape.N);

    // 4. 16-FSK 解调 -> codedBits（FEC 前的 bit 流）
    std::vector<uint8_t> codedBits;
    codedBits.reserve(static_cast<size_t>((totalSymbols - params.syncSymbols) * 4)); // 1 符号 4bit

    for (uint64_t symIdx = 0; symIdx < totalSymbols; ++symIdx) {
        if (!ifs.read(reinterpret_cast<char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size() * sizeof(int16_t)))) {
            std::cerr << "Unexpected end of WAV data.\n";
            break;
        }

        // 前 syncSymbols 个符号作为同步，扔掉
        if (symIdx < static_cast<uint64_t>(params.syncSymbols)) {
            continue;
        }

        // DC 去除 + Hann 窗
        preprocessFrame(frame);

        int symbolIndex = detectSymbolIndex(frame.data(), cfgs); // 0..15

        // 还原 4 bit（顺序与编码端完全一致：b3,b2,b1,b0）
        for (int bitPos = 3; bitPos >= 0; --bitPos) {
            int bit = (symbolIndex >> bitPos) & 0x1;
            codedBits.push_back(static_cast<uint8_t>(bit));
        }
    }

    if (codedBits.empty()) {
        std::cerr << "No coded bits decoded from FSK.\n";
        return false;
    }

    // 5. 卷积 Viterbi 解码 -> 原始帧 bit 流
    std::vector<uint8_t> bits;
    if (!convDecode(codedBits, bits)) {
        std::cerr << "Convolutional decode failed.\n";
        return false;
    }

    // 6. bit 流 -> frameBytes
    std::vector<uint8_t> frameBytes;
    bitsToBytes(bits, frameBytes);

    // 7. 帧解析（marker/length/CRC）
    std::vector<uint8_t> payload;
    uint8_t seq = 0;
    if (!parseFrame(frameBytes, payload, seq)) {
        std::cerr << "Frame parse failed (marker or CRC error).\n";
        return false;
    }

    // 8. 写回原始 payload
    std::ofstream ofs_out(outputBinPath, std::ios::binary);
    if (!ofs_out) {
        std::cerr << "Failed to open output file: " << outputBinPath << "\n";
        return false;
    }
    ofs_out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));

    std::cout << "Decoded " << payload.size()
              << " payload bytes (Frame+FEC+16-FSK DFT-bin) to "
              << outputBinPath << "\n";
    return true;
}