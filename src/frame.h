#pragma once
#include <vector>
#include <cstdint>

// 帧格式：
// [0] marker1 = 0xA5
// [1] marker2 = 0x5A
// [2] len_lo
// [3] len_hi   -> uint16_t payloadLen
// [4] seq      -> 帧号
// [5..] payload bytes
// [最后2字节] CRC16(frame[0..len+4])  不含 CRC 自己

std::vector<uint8_t> buildFrame(const std::vector<uint8_t>& payload, uint8_t seq);

bool parseFrame(const std::vector<uint8_t>& frame,
                std::vector<uint8_t>& payloadOut,
                uint8_t& seqOut);