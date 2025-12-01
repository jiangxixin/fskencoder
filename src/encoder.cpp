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
    bytesToBits(frame, bits);

    // 4. 卷积编码 FEC
    std::vector<uint8_t> codedBits;
    convEncode(bits, codedBits);

    // 5. FEC 后的 bit 流打包成字节 -> codedBytes
    std::vector<uint8_t> codedBytes;
    bitsToBytes(codedBits, codedBytes);

    // 6. 符号形状 + LUT
    SymbolShape shape;
    try {
        shape = computeSymbolShape(params.sampleRate, params.symbolDurationSec);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }

    std::array<std::vector<int16_t>, 16> waves;
    buildSymbolLUT(waves, params, shape);

    // 每字节 8bit -> 两个 4bit 符号
    uint64_t dataSymbols  = static_cast<uint64_t>(codedBytes.size()) * 2ULL;
    uint64_t totalSymbols = static_cast<uint64_t>(params.syncSymbols) + dataSymbols;
    uint64_t totalSamples = totalSymbols * shape.N;

    // 7. 写 WAV 头
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

    // 8. 写前导同步符号（0 和 15 交替）
    for (int i = 0; i < params.syncSymbols; ++i) {
        int sym = (i % 2 == 0) ? 0 : 15;
        writeSymbol(ofs, waves, sym);
        if (!ofs) {
            std::cerr << "Failed while writing sync symbols.\n";
            return false;
        }
    }

    // 9. 写数据符号（每字节两个 symbol：高 nibble & 低 nibble）
    for (uint8_t byte : codedBytes) {
        int highNibble = (byte >> 4) & 0x0F;   // b7..b4
        int lowNibble  = byte        & 0x0F;   // b3..b0

        writeSymbol(ofs, waves, highNibble);
        if (!ofs) {
            std::cerr << "Failed while writing data symbols (high).\n";
            return false;
        }

        writeSymbol(ofs, waves, lowNibble);
        if (!ofs) {
            std::cerr << "Failed while writing data symbols (low).\n";
            return false;
        }
    }

    std::cout << "Encoded " << payload.size()
              << " bytes payload (frame+FEC+16-FSK) to " << outputWavPath << "\n";
    return true;
}