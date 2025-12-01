// src/main.cpp
#include "encoder.h"
#include "decoder.h"

#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdlib>

static void printUsage(const char* prog) {
    std::cout << "Usage:\n"
              << "  Encode (16-FSK DFT-bin + Frame + FEC):\n"
              << "    " << prog << " encode -i <input.bin> -o <output.wav> [options]\n"
              << "  Decode (16-FSK DFT-bin + Frame + FEC):\n"
              << "    " << prog << " decode -i <input.wav> -o <output.bin> [options]\n"
              << "\nOptions (encode & decode):\n"
              << "    --sr <sampleRate>          (default 44100)\n"
              << "    --symdur <seconds>         (default 0.001, symbol duration)\n"
              << "    --bitdur <seconds>         (alias of --symdur)\n"
              << "    --sync <symbols>           (default 64, number of sync symbols)\n"
              << "    --bin0  <k>                (DFT bin index for symbol 0)\n"
              << "    --bin1  <k>                ...\n"
              << "    --bin15 <k>                (DFT bin index for symbol 15)\n"
              << "        # 实际频率 f_k = bin_k * sr / N, N = symdur * sr\n"
              << "\nEncode-only options:\n"
              << "    --amp <amplitude>          (default 12000, 16-bit PCM amplitude)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "encode") {
        std::string inputBin;
        std::string outputWav;
        EncodeParams params; // 带默认值

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            auto needValue = [&](const std::string& a) {
                if (i + 1 >= argc) {
                    std::cerr << "Option " << a << " requires a value.\n";
                    std::exit(1);
                }
            };

            if (arg == "-i") {
                needValue(arg);
                inputBin = argv[++i];
            } else if (arg == "-o") {
                needValue(arg);
                outputWav = argv[++i];
            } else if (arg == "--sr") {
                needValue(arg);
                params.sampleRate = static_cast<uint32_t>(std::stoul(argv[++i]));
            } else if (arg == "--symdur" || arg == "--bitdur") {
                needValue(arg);
                params.symbolDurationSec = std::stod(argv[++i]);
            } else if (arg == "--sync") {
                needValue(arg);
                params.syncSymbols = std::stoi(argv[++i]);
            } else if (arg == "--amp") {
                needValue(arg);
                params.amplitude = static_cast<int16_t>(std::stoi(argv[++i]));
            } else if (arg.rfind("--bin", 0) == 0) {
                // 解析 --bin0 .. --bin15
                // arg 形如 "--bin0" 或 "--bin10"
                needValue(arg);
                std::string idxStr = arg.substr(5); // 去掉前缀 "--bin"
                int idx = std::stoi(idxStr);
                if (idx < 0 || idx >= 16) {
                    std::cerr << "Bin index out of range (0..15): " << idx << "\n";
                    return 1;
                }
                params.bins[idx] = std::stoi(argv[++i]);
            } else {
                std::cerr << "Unknown option: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }

        if (inputBin.empty() || outputWav.empty()) {
            std::cerr << "Both -i and -o are required for encode.\n";
            printUsage(argv[0]);
            return 1;
        }

        if (!encodeFileToWav(inputBin, outputWav, params)) {
            std::cerr << "Encode failed.\n";
            return 1;
        }
        return 0;

    } else if (mode == "decode") {
        std::string inputWav;
        std::string outputBin;
        DecodeParams params; // 带默认值

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            auto needValue = [&](const std::string& a) {
                if (i + 1 >= argc) {
                    std::cerr << "Option " << a << " requires a value.\n";
                    std::exit(1);
                }
            };

            if (arg == "-i") {
                needValue(arg);
                inputWav = argv[++i];
            } else if (arg == "-o") {
                needValue(arg);
                outputBin = argv[++i];
            } else if (arg == "--sr") {
                needValue(arg);
                params.sampleRate = static_cast<uint32_t>(std::stoul(argv[++i]));
            } else if (arg == "--symdur" || arg == "--bitdur") {
                needValue(arg);
                params.symbolDurationSec = std::stod(argv[++i]);
            } else if (arg == "--sync") {
                needValue(arg);
                params.syncSymbols = std::stoi(argv[++i]);
            } else if (arg.rfind("--bin", 0) == 0) {
                needValue(arg);
                std::string idxStr = arg.substr(5); // "--bin" 长度为5
                int idx = std::stoi(idxStr);
                if (idx < 0 || idx >= 16) {
                    std::cerr << "Bin index out of range (0..15): " << idx << "\n";
                    return 1;
                }
                params.bins[idx] = std::stoi(argv[++i]);
            } else {
                std::cerr << "Unknown option: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }

        if (inputWav.empty() || outputBin.empty()) {
            std::cerr << "Both -i and -o are required for decode.\n";
            printUsage(argv[0]);
            return 1;
        }

        if (!decodeWavToFile(inputWav, outputBin, params)) {
            std::cerr << "Decode failed.\n";
            return 1;
        }
        return 0;

    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        printUsage(argv[0]);
        return 1;
    }
}