#include "fec.h"
#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>

void bytesToBits(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& bits) {
    bits.clear();
    bits.reserve(bytes.size() * 8);
    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back((b >> i) & 0x1);
        }
    }
}

void bitsToBytes(const std::vector<uint8_t>& bits, std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (bits.empty()) return;

    size_t numBytes = (bits.size() + 7) / 8;
    bytes.assign(numBytes, 0);

    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            size_t byteIdx = i / 8;
            int bitPos = 7 - static_cast<int>(i % 8);
            bytes[byteIdx] |= static_cast<uint8_t>(1u << bitPos);
        }
    }
}

// K=3，状态编码：state = (u_{k-1} << 1) | u_{k-2}
static void convEncodeBit(uint8_t u, uint8_t& state, std::vector<uint8_t>& outBits) {
    u &= 0x1;
    uint8_t s1 = (state >> 1) & 0x1; // u_{k-1}
    uint8_t s2 =  state       & 0x1; // u_{k-2}

    // G1 = 111b => v0 = u ⊕ s1 ⊕ s2
    uint8_t v0 = u ^ s1 ^ s2;
    // G2 = 101b => v1 = u ⊕ s2
    uint8_t v1 = u ^ s2;

    outBits.push_back(v0);
    outBits.push_back(v1);

    // 下一状态：state' = (u << 1) | s1
    state = static_cast<uint8_t>(((u & 0x1) << 1) | s1);
}

void convEncode(const std::vector<uint8_t>& inBits, std::vector<uint8_t>& outBits) {
    outBits.clear();
    if (inBits.empty()) return;

    constexpr int K = 3;
    uint8_t state = 0; // 初始状态 00

    // 正常数据
    for (uint8_t b : inBits) {
        convEncodeBit(b & 0x1, state, outBits);
    }

    // 尾比特：K-1 个 0，把状态冲洗回 0
    for (int i = 0; i < K - 1; ++i) {
        convEncodeBit(0, state, outBits);
    }
}

// -------------------- Viterbi 解码 --------------------

bool convDecode(const std::vector<uint8_t>& inBits, std::vector<uint8_t>& outBits) {
    outBits.clear();
    if (inBits.empty() || (inBits.size() % 2) != 0) {
        return false;
    }

    constexpr int K = 3;
    constexpr int NUM_STATES = 1 << (K - 1); // 4
    const int INF = std::numeric_limits<int>::max() / 4;

    size_t steps = inBits.size() / 2; // 每两比特对应一个时刻

    // pathMetric[t][s]
    std::vector<std::array<int, NUM_STATES>> pathMetric(steps + 1);
    std::vector<std::array<uint8_t, NUM_STATES>> prevState(steps + 1);
    std::vector<std::array<uint8_t, NUM_STATES>> prevInputBit(steps + 1);

    for (int s = 0; s < NUM_STATES; ++s) {
        pathMetric[0][s] = (s == 0) ? 0 : INF; // 初始状态为 0
    }

    // 预计算转移
    struct Branch {
        uint8_t out0;
        uint8_t out1;
        uint8_t nextState;
    };
    std::array<std::array<Branch, 2>, NUM_STATES> branches{};
    for (int s = 0; s < NUM_STATES; ++s) {
        uint8_t s1 = (s >> 1) & 0x1;
        uint8_t s2 =  s       & 0x1;
        for (int u = 0; u < 2; ++u) {
            uint8_t uu = static_cast<uint8_t>(u);
            uint8_t v0 = uu ^ s1 ^ s2;          // G1
            uint8_t v1 = uu ^ s2;               // G2
            uint8_t ns = static_cast<uint8_t>(((uu & 0x1) << 1) | s1);

            branches[s][u] = Branch{v0, v1, ns};
        }
    }

    // 递推
    for (size_t t = 0; t < steps; ++t) {
        uint8_t r0 = inBits[2 * t]     & 0x1;
        uint8_t r1 = inBits[2 * t + 1] & 0x1;

        for (int s = 0; s < NUM_STATES; ++s) {
            pathMetric[t + 1][s] = INF;
        }

        for (int s = 0; s < NUM_STATES; ++s) {
            int pm = pathMetric[t][s];
            if (pm >= INF) continue;

            for (int u = 0; u < 2; ++u) {
                const Branch& br = branches[s][u];
                int dist = (br.out0 != r0) + (br.out1 != r1); // Hamming 距离
                int cand = pm + dist;
                uint8_t ns = br.nextState;

                if (cand < pathMetric[t + 1][ns]) {
                    pathMetric[t + 1][ns]    = cand;
                    prevState[t + 1][ns]     = static_cast<uint8_t>(s);
                    prevInputBit[t + 1][ns]  = static_cast<uint8_t>(u);
                }
            }
        }
    }

    // 编码时用尾比特把状态冲洗回 0，所以终点最好选 state=0
    int bestState = 0;
    int bestMetric = pathMetric[steps][0];
    if (bestMetric >= INF) {
        return false;
    }

    // 回溯
    std::vector<uint8_t> allBits;
    allBits.reserve(steps);
    int curState = bestState;
    for (size_t t = steps; t > 0; --t) {
        uint8_t u = prevInputBit[t][curState];
        allBits.push_back(u);
        curState = prevState[t][curState];
    }
    std::reverse(allBits.begin(), allBits.end());

    // 去尾比特 K-1
    if (allBits.size() <= (K - 1)) {
        return false;
    }
    allBits.resize(allBits.size() - (K - 1));

    outBits = std::move(allBits);
    return true;
}