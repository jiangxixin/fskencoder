cmake_minimum_required(VERSION 3.10)
project(audio_codec LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(audio_codec
    src/main.cpp
    src/encoder.cpp
    src/decoder.cpp
    src/wav_io.cpp
    src/fec.cpp
    src/frame.cpp
)

if (MSVC)
    target_compile_options(audio_codec PRIVATE /W4)
else()
    target_compile_options(audio_codec PRIVATE -Wall -Wextra -pedantic)
endif()