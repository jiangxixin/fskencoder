#include "wav_io.h"
#include <fstream>
#include <iostream>
#include <string>

bool writeWavMono16(
    const std::string& path,
    const std::vector<int16_t>& samples,
    uint32_t sampleRate
) {
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

    header.subchunk2Size = static_cast<uint32_t>(samples.size() * header.blockAlign);
    header.chunkSize     = 36 + header.subchunk2Size;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open WAV for writing: " << path << "\n";
        return false;
    }

    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
    ofs.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));

    return true;
}

bool readWavMono16(
    const std::string& path,
    std::vector<int16_t>& samples,
    uint32_t& sampleRate
) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "Failed to open WAV for reading: " << path << "\n";
        return false;
    }

    WavHeader header{};
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!ifs) {
        std::cerr << "Failed to read WAV header: " << path << "\n";
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

    sampleRate = header.sampleRate;
    const size_t numSamples = header.subchunk2Size / (header.bitsPerSample / 8);
    samples.resize(numSamples);
    ifs.read(reinterpret_cast<char*>(samples.data()), header.subchunk2Size);

    if (!ifs) {
        std::cerr << "Failed to read WAV samples\n";
        return false;
    }
    return true;
}