#include "encoder.h"
#include "decoder.h"
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdlib>

static void printUsage(const char* prog) {
    std::cout << "Usage:\n"
              << "  Encode (16-FSK + Frame + FEC):\n"
              << "    " << prog << " encode -i <input.bin> -o <output.wav> [options]\n"
              << "  Decode (16-FSK + Frame + FEC):\n"
              << "    " << prog << " decode -i <input.wav> -o <output.bin> [options]\n"
              << "\nOptions (encode & decode):\n"
              << "    --sr <sampleRate>          (default 44100)\n"
              << "    --symdur <seconds>         (default 0.001, symbol duration)\n"
              << "    --bitdur <seconds>         (alias of --symdur)\n"
              << "    --sync <symbols>           (default 64, number of sync symbols)\n"
              << "    --f0  <freqHz>             (default 2000)\n"
              << "    --f1  <freqHz>             (default 2300)\n"
              << "    --f2  <freqHz>             (default 2600)\n"
              << "    --f3  <freqHz>             (default 2900)\n"
              << "    --f4  <freqHz>             (default 3200)\n"
              << "    --f5  <freqHz>             (default 3500)\n"
              << "    --f6  <freqHz>             (default 3800)\n"
              << "    --f7  <freqHz>             (default 4100)\n"
              << "    --f8  <freqHz>             (default 4400)\n"
              << "    --f9  <freqHz>             (default 4700)\n"
              << "    --f10 <freqHz>             (default 5000)\n"
              << "    --f11 <freqHz>             (default 5300)\n"
              << "    --f12 <freqHz>             (default 5600)\n"
              << "    --f13 <freqHz>             (default 5900)\n"
              << "    --f14 <freqHz>             (default 6200)\n"
              << "    --f15 <freqHz>             (default 6500)\n"
              << "\nEncode-only options:\n"
              << "    --amp <amplitude>          (default 12000)\n";
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
        EncodeParams params;

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
            } else if (arg.rfind("--f", 0) == 0 && arg.size() >= 3) {
                needValue(arg);
                // arg like "--f0", "--f10"
                int idx = std::stoi(arg.substr(3));
                if (idx < 0 || idx >= 16) {
                    std::cerr << "Frequency index out of range: " << idx << "\n";
                    return 1;
                }
                params.freqs[idx] = std::stod(argv[++i]);
            } else if (arg == "--amp") {
                needValue(arg);
                params.amplitude = static_cast<int16_t>(std::stoi(argv[++i]));
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
        DecodeParams params;

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
            } else if (arg.rfind("--f", 0) == 0 && arg.size() >= 3) {
                needValue(arg);
                int idx = std::stoi(arg.substr(3));
                if (idx < 0 || idx >= 16) {
                    std::cerr << "Frequency index out of range: " << idx << "\n";
                    return 1;
                }
                params.freqs[idx] = std::stod(argv[++i]);
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