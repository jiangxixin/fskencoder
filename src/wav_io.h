#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct WavHeader {
    char     riff[4];        // "RIFF"
    uint32_t chunkSize;
    char     wave[4];        // "WAVE"
    char     fmt[4];         // "fmt "
    uint32_t subchunk1Size;  // 16 for PCM
    uint16_t audioFormat;    // 1 for PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     data[4];        // "data"
    uint32_t subchunk2Size;
};

// 这些函数现在主要给你备用，encoder/decoder 里只复用了 WavHeader 定义。
// 如果需要简单读写 wav，也可以用它们。

bool writeWavMono16(
    const std::string& path,
    const std::vector<int16_t>& samples,
    uint32_t sampleRate
);

bool readWavMono16(
    const std::string& path,
    std::vector<int16_t>& samples,
    uint32_t& sampleRate
);