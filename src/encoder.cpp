#include "encoder.h"
#include "wav_io.h"
#include "fec.h"
#include "frame.h"

#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <iterator>
#include <array>

namespace {

constexpr double PI = 3.14159265358979323846;

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

// 预计算 16 个频率对应的“一个符号波形” LUT
void buildSymbolLUT(
    std::array<std::vector<int16_t>, 16>& waves,
    const EncodeParams& params,
    const SymbolShape& shape
) {
    for (int i = 0; i < 16; ++i) {
        waves[i].resize(shape.N);
        double f = params.freqs[i];
        for (uint32_t n = 0; n < shape.N; ++n) {
            double t = static_cast<double>(n) / params.sampleRate;
            double v = params.amplitude * std::sin(2.0 * PI * f * t);
            waves[i][n] = static_cast<int16_t>(std::round(v));
        }
    }
}

// 写一个符号
inline void writeSymbol(
    std::ofstream& ofs,
    const std::array<std::vector<int16_t>, 16>& waves,
    int symbolIndex
) {
    const auto& w = waves[symbolIndex & 0xF];
    ofs.write(reinterpret_cast<const char*>(w.data()),
              static_cast<std::streamsize>(w.size() * sizeof(int16_t)));
}

// 构造 WAV 头
WavHeader makeWavHeader(uint32_t sampleRate, uint64_t totalSamples) {
    WavHeader header{};
    header.riff[0] = 'R'; header.riff[1] = 'I';
    header.riff[2] = 'F'; header.riff[3] = 'F';
    header.wave[0] = 'W'; header.wave[1] = 'A';
    header.wave[2] = 'V'; header.wave[3] = 'E';
    header.fmt[0]  = 'f'; header.fmt[1]  = 'm';
    header.fmt[2]  = 't'; header.fmt[3]  = ' ';
    header.data[0] = 'd'; header.data[1] = 'a';
    header.data[2] = 't'; header.data[3] = 'a';

    header.subchunk1Size = 16;
    header.audioFormat   = 1;
    header.numChannels   = 1;
    header.sampleRate    = sampleRate;
    header.bitsPerSample = 16;
    header.byteRate      = sampleRate * header.numChannels * header.bitsPerSample / 8;
    header.blockAlign    = header.numChannels * header.bitsPerSample / 8;

    uint64_t dataBytes = totalSamples * header.blockAlign;
    if (dataBytes > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("WAV data too large (>4GB), not supported");
    }

    header.subchunk2Size = static_cast<uint32_t>(dataBytes);
    header.chunkSize     = 36 + header.subchunk2Size;
    return header;
}

} // namespace

bool encodeFileToWav(
    const std::string& inputBinPath,
    const std::string& outputWavPath,
    const EncodeParams& params
) {
    // 1. 读取原始二进制 -> payload
    std::ifstream ifs(inputBinPath, std::ios::binary);
    if (!ifs) {
        std::cerr << "Failed to open input file: " << inputBinPath << "\n";
        return false;
    }
    std::vector<uint8_t> payload(
        (std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>()
    );
    if (payload.empty()) {
        std::cerr << "Input file is empty.\n";
        return false;
    }

    // 2. 构造单帧（加帧头 + CRC）
    uint8_t seq = 0; // 单帧场景，先用 0
    std::vector<uint8_t> frame = buildFrame(payload, seq);

    // 3. 帧字节 -> bit 流
    std::vector<uint8_t> bits;
    bytesToBits(frame, bits);  // bits.size() = 8 * frame.size()

    // 4. 卷积编码 FEC
    std::vector<uint8_t> codedBits;
    convEncode(bits, codedBits);

    // VERY IMPORTANT:
    // 这里 codedBits 的长度一定是 4 的倍数（可以检查一下）
    if (codedBits.size() % 4 != 0) {
        std::cerr << "Internal error: codedBits.size() not multiple of 4\n";
        return false;
    }
    const uint64_t dataSymbols = codedBits.size() / 4; // 每 4bit 一个 16-FSK 符号

    // 5. 符号形状 + LUT
    SymbolShape shape;
    try {
        shape = computeSymbolShape(params.sampleRate, params.symbolDurationSec);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }

    std::array<std::vector<int16_t>, 16> waves;
    buildSymbolLUT(waves, params, shape);

    uint64_t totalSymbols = static_cast<uint64_t>(params.syncSymbols) + dataSymbols;
    uint64_t totalSamples = totalSymbols * shape.N;

    // 6. 写 WAV 头
    WavHeader header;
    try {
        header = makeWavHeader(params.sampleRate, totalSamples);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }

    std::ofstream ofs(outputWavPath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open WAV for writing: " << outputWavPath << "\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!ofs) {
        std::cerr << "Failed to write WAV header.\n";
        return false;
    }

    // 7. 写前导同步符号（0 和 15 交替）
    for (int i = 0; i < params.syncSymbols; ++i) {
        int sym = (i % 2 == 0) ? 0 : 15;
        writeSymbol(ofs, waves, sym);
        if (!ofs) {
            std::cerr << "Failed while writing sync symbols.\n";
            return false;
        }
    }

    // 8. 写数据符号：每 4 bit -> 1 个 0..15 的 symbolIndex
    for (uint64_t symIdx = 0; symIdx < dataSymbols; ++symIdx) {
        size_t base = static_cast<size_t>(symIdx * 4);
        uint8_t b3 = codedBits[base]     & 0x1; // 最高 bit
        uint8_t b2 = codedBits[base + 1] & 0x1;
        uint8_t b1 = codedBits[base + 2] & 0x1;
        uint8_t b0 = codedBits[base + 3] & 0x1; // 最低 bit

        uint8_t symbolIndex = static_cast<uint8_t>((b3 << 3) | (b2 << 2) | (b1 << 1) | b0);
        writeSymbol(ofs, waves, symbolIndex);
        if (!ofs) {
            std::cerr << "Failed while writing data symbols.\n";
            return false;
        }
    }

    std::cout << "Encoded " << payload.size()
              << " bytes payload (frame+FEC+16-FSK) to " << outputWavPath << "\n";
    return true;
}