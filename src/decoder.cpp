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

// Goertzel 配置
struct GoertzelConfig {
    float coeff;
    uint32_t N;
};

GoertzelConfig makeGoertzelConfig(
    uint32_t sampleRate,
    double targetFreq,
    uint32_t N
) {
    GoertzelConfig cfg{};
    cfg.N = N;

    float k     = 0.5f + static_cast<float>(N * targetFreq / sampleRate);
    float omega = 2.0f * PI_F * k / static_cast<float>(N);
    cfg.coeff   = 2.0f * std::cos(omega);
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

    // 3. 预计算 16 个频率的 Goertzel 配置
    std::array<GoertzelConfig, 16> cfgs;
    for (int i = 0; i < 16; ++i) {
        cfgs[i] = makeGoertzelConfig(params.sampleRate, params.freqs[i], shape.N);
    }

    std::vector<int16_t> frame(shape.N);

    // 4. 16-FSK 解调 -> codedBytes
    std::vector<uint8_t> codedBytes;
    codedBytes.reserve(static_cast<size_t>((totalSymbols - params.syncSymbols) / 2));

    uint8_t curByte = 0;
    int bitCountInByte = 0;

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

        int symbolIndex = detectSymbolIndex(frame.data(), cfgs); // 0..15

        // 还原 4 bit（高 bit 在前）
        for (int k = 3; k >= 0; --k) {
            int bit = (symbolIndex >> k) & 0x1;
            curByte |= (static_cast<uint8_t>(bit) << (7 - bitCountInByte));
            ++bitCountInByte;
            if (bitCountInByte == 8) {
                codedBytes.push_back(curByte);
                curByte = 0;
                bitCountInByte = 0;
            }
        }
    }

    if (codedBytes.empty()) {
        std::cerr << "No coded bytes decoded from FSK.\n";
        return false;
    }

    // 5. codedBytes -> codedBits
    std::vector<uint8_t> codedBits;
    bytesToBits(codedBytes, codedBits);

    // 6. 卷积 Viterbi 解码 -> bits
    std::vector<uint8_t> bits;
    if (!convDecode(codedBits, bits)) {
        std::cerr << "Convolutional decode failed.\n";
        return false;
    }

    // 7. bits -> frameBytes
    std::vector<uint8_t> frameBytes;
    bitsToBytes(bits, frameBytes);

    // 8. 帧解析（marker/长度/CRC）
    std::vector<uint8_t> payload;
    uint8_t seq = 0;
    if (!parseFrame(frameBytes, payload, seq)) {
        std::cerr << "Frame parse failed.\n";
        return false;
    }

    // 9. 写回原始 payload
    std::ofstream ofs_out(outputBinPath, std::ios::binary);
    if (!ofs_out) {
        std::cerr << "Failed to open output file: " << outputBinPath << "\n";
        return false;
    }
    ofs_out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));

    std::cout << "Decoded " << payload.size()
              << " payload bytes (Frame+FEC+16-FSK) to "
              << outputBinPath << "\n";
    return true;
}