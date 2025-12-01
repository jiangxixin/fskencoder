#include "frame.h"
#include "crc16.h"
#include <iostream>
#include <stdexcept>

std::vector<uint8_t> buildFrame(const std::vector<uint8_t>& payload, uint8_t seq) {
    if (payload.size() > 0xFFFF) {
        throw std::runtime_error("Payload too large for uint16 length");
    }

    uint16_t len = static_cast<uint16_t>(payload.size());
    std::vector<uint8_t> frame;
    frame.reserve(5 + payload.size() + 2);

    frame.push_back(0xA5);
    frame.push_back(0x5A);
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(seq);

    frame.insert(frame.end(), payload.begin(), payload.end());

    uint16_t crc = crc16_ccitt(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));

    return frame;
}

bool parseFrame(const std::vector<uint8_t>& frame,
                std::vector<uint8_t>& payloadOut,
                uint8_t& seqOut) {
    payloadOut.clear();

    if (frame.size() < 5 + 2) {
        std::cerr << "Frame too short\n";
        return false;
    }

    if (frame[0] != 0xA5 || frame[1] != 0x5A) {
        std::cerr << "Frame marker mismatch\n";
        return false;
    }

    uint16_t len = static_cast<uint16_t>(frame[2])
                 | (static_cast<uint16_t>(frame[3]) << 8);
    seqOut = frame[4];

    size_t headerSize   = 5;
    size_t expectedSize = headerSize + len + 2;
    if (frame.size() < expectedSize) {
        std::cerr << "Frame length mismatch\n";
        return false;
    }

    size_t crcPos = expectedSize - 2;
    uint16_t crcRecv = static_cast<uint16_t>(frame[crcPos] << 8)
                     | static_cast<uint16_t>(frame[crcPos + 1]);
    uint16_t crcCalc = crc16_ccitt(frame.data(), expectedSize - 2);

    if (crcRecv != crcCalc) {
        std::cerr << "Frame CRC mismatch\n";
        return false;
    }

    payloadOut.assign(frame.begin() + headerSize, frame.begin() + headerSize + len);
    return true;
}