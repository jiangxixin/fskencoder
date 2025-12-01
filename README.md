# Audio 16-FSK File Codec

一个用 C++/CMake 实现的**高性能二进制文件 ⇄ 音频(WAV) 编解码器**。

特点：

- 💿 **任意二进制文件 → 16-FSK 调制的 WAV 音频**
- 💾 **WAV 音频 → 还原原始二进制文件**
- 📡 物理层：**16-FSK（一符号 4 bit）+ Goertzel 解调**
- 🛡 链路层：**卷积码 FEC (rate 1/2, K=3) + 单帧帧头 + CRC16**
- ⚙️ 完整命令行参数可调：采样率 / 符号时长 / 16 个频点 / 同步符号数 / 幅度等
- 📦 代码纯 C++17，无第三方依赖，跨平台（Linux / macOS / Windows）

---

## 1. 工程结构

```text
audio_codec/
├── CMakeLists.txt
└── src
    ├── main.cpp          # 命令行入口
    ├── wav_io.h/.cpp     # WAV 头结构 & 简单读写
    ├── crc16.h           # CRC-16-CCITT 实现
    ├── fec.h/.cpp        # 卷积码 FEC + bit/byte 转换
    ├── frame.h/.cpp      # 帧结构封装 & 解析 (marker + len + seq + CRC)
    ├── encoder.h/.cpp    # 文件 -> Frame -> FEC -> 16-FSK -> WAV
    └── decoder.h/.cpp    # WAV -> 16-FSK -> FEC 解码 -> Frame -> 文件


⸻

2. 编译说明

2.1 依赖
	•	CMake ≥ 3.10
	•	C++17 编译器
	•	Linux / macOS：g++ / clang++
	•	Windows：
	•	推荐 Visual Studio 2019/2022 + “使用 C++ 的桌面开发”工作负载
	•	在 “x64 Native Tools Command Prompt for VS” 中运行 CMake

2.2 编译步骤

在工程根目录执行：

mkdir build
cd build
cmake ..
cmake --build .

成功后，会在 build/ 目录下生成可执行文件：
	•	Windows: audio_codec.exe
	•	Linux/macOS: audio_codec

⸻

3. 使用方法

3.1 编码：二进制 → 音频

audio_codec encode -i <input.bin> -o <output.wav> [options]

示例：

# 16-FSK，符号时长 1ms，约 4 kbps
audio_codec encode -i ../test.bin -o test_16fsk_fec.wav \
    --sr 44100 --symdur 0.001 --sync 64 --amp 12000

3.2 解码：音频 → 二进制

audio_codec decode -i <input.wav> -o <output.bin> [options]

示例：

audio_codec decode -i test_16fsk_fec.wav -o restored.bin \
    --sr 44100 --symdur 0.001 --sync 64

3.3 校验传输是否正确

# Linux / macOS
cmp ../test.bin restored.bin

# Windows
fc /b ..\test.bin restored.bin


⸻

4. 命令行参数

编码和解码共用的一组参数（必须保持一致）：
	•	--sr <sampleRate>
采样率，默认 44100 Hz。
	•	--symdur <seconds> / --bitdur <seconds>
符号时长（秒），默认 0.001（1 ms）。
	•	每个符号携带 4 bit（16-FSK），理论码率约为：
bitrate ≈ 4 / symdur (bit/s)
	•	symdur=0.001 → ≈ 4000 bit/s
	•	symdur=0.0005 → ≈ 8000 bit/s（误码率可能上升，需要 SNR 较好）
	•	--sync <symbols>
前导同步符号数量，默认 64。
这些符号用固定模式（0 和 15 交替）占据开头一段，用于接收侧“热身”和对齐。
	•	--f0 .. --f15 <freqHz>
16 个频率，对应 4bit 符号 0000b .. 1111b（即 0..15）。
默认值：

f0  = 2000 Hz
f1  = 2300 Hz
f2  = 2600 Hz
f3  = 2900 Hz
f4  = 3200 Hz
f5  = 3500 Hz
f6  = 3800 Hz
f7  = 4100 Hz
f8  = 4400 Hz
f9  = 4700 Hz
f10 = 5000 Hz
f11 = 5300 Hz
f12 = 5600 Hz
f13 = 5900 Hz
f14 = 6200 Hz
f15 = 6500 Hz



编码专用参数：
	•	--amp <amplitude>
正弦波幅度，默认 12000（16-bit PCM 范围 -32768~32767 中的中等水平）。
如果出现削波（clipping），可以适当减小。

⸻

5. 协议与内部流程

5.1 发送端流水线
	1.	原始文件 → payload（字节数组）
	2.	buildFrame(payload, seq) 构造单帧：

[0]  marker1 = 0xA5
[1]  marker2 = 0x5A
[2]  len_lo
[3]  len_hi       -> uint16_t payloadLen
[4]  seq          -> 帧号，目前固定为 0
[5..] payload     -> 文件内容
[最后2字节] CRC16(frame[0..len+4])


	3.	帧字节 → bit 流（高位在前）
	4.	卷积编码（FEC）：
	•	rate = 1/2, K = 3
	•	生成多一倍的比特，并附加尾比特把状态冲洗到 0
	5.	FEC 输出 bit 流 → 再打包成字节 codedBytes
	6.	16-FSK 调制：
	•	每字节 8 bit → 2 个符号，分别是高 4bit & 低 4bit
	•	每个 4bit（0..15）映射到一个频率 freqs[index]
	•	对每个符号生成一段长度 symbolDurationSec 的正弦波（使用 LUT 预计算）
	7.	前面加上 syncSymbols 个同步符号（0 和 15 交替）
	8.	写入 WAV 头 + PCM 数据，输出单声道 16bit WAV 文件。

5.2 接收端流水线
	1.	从 WAV 中读出 WavHeader，检查：
	•	RIFF/WAVE/fmt /data
	•	单声道、16bit、采样率与参数一致
	2.	利用 symbolDurationSec 和 sampleRate 计算每符号采样点数 N
	3.	按符号逐段读取 PCM（流式，不占用大内存）：
	•	对每段 N 个样本，分别用 16 个 Goertzel 滤波器计算能量
	•	选能量最大的 index 作为当前符号对应的 4bit 值
	4.	丢弃前 syncSymbols 个符号，剩余符号拼成字节流 codedBytes
	5.	codedBytes → 展开成 FEC bit 流 codedBits
	6.	Viterbi 解码（硬判决定距）：
	•	纠正部分符号/bit 错误，恢复信息 bit 流 bits
	7.	把 bits 打包成 frameBytes
	8.	parseFrame(frameBytes)：
	•	校验帧头 marker
	•	检查长度字段
	•	校验 CRC16（帧头+payload）
	9.	若帧合法，取出 payload 即原始文件内容，保存至输出二进制文件。

⸻

6. 调参建议
	•	想要更高码率：
	•	减小 --symdur，比如：
	•	0.001 → 约 4 kbps
	•	0.0007 → 约 5.7 kbps
	•	0.0005 → 约 8 kbps
	•	同时可以视情况提高 --sr（如 48000/96000），以保证每个符号内有足够的载波周期。
	•	想要更稳的解码：
	•	增大 --symdur（符号更长，判决更可靠）
	•	增大 --sync，给接收端更多“热身”符号
	•	适当拉大频率间隔（比如 2k, 3k, 4k, 5k, …）
	•	如果以后要走真实扬声器+麦克风链路：
	•	避开声卡/喇叭响应较差的频率段
	•	频带尽量放在 1–6 kHz 区间
	•	可能需要更复杂的同步策略 & 自动增益 / 自适应滤波

⸻

7. Todo / 扩展方向

若后续想继续折腾，可以考虑：
	•	多帧支持 + 简单 ARQ 协议（重传机制）
	•	软判决 Viterbi（使用能量差构造 LLR，提高纠错能力）
	•	更高阶调制（例如 32-FSK / QAM 等）
	•	实时音频接口（声卡实时发送 / 接收）

⸻