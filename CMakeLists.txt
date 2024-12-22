cmake_minimum_required(VERSION 3.29)
project(recorder)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
        wil
        GIT_REPOSITORY https://github.com/microsoft/wil.git
)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.0
)

FetchContent_Declare(
        hmac_sha256
        GIT_REPOSITORY https://github.com/h5p9sl/hmac_sha256.git
)

FetchContent_Declare(
        opus
        GIT_REPOSITORY https://github.com/xiph/opus.git
        GIT_TAG v1.5.2
)

FetchContent_Declare(
        ogg
        GIT_REPOSITORY https://github.com/xiph/ogg.git
        GIT_TAG v1.3.5
)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
)

FetchContent_MakeAvailable(wil spdlog hmac_sha256 opus ogg googletest)

set(CXX_SCAN_FOR_MODULES ON)
add_definitions(-DNOMINMAX)
add_executable(recorder
        src/main.cpp src/hwid.hpp
        src/OggOpusWriter.hpp
        src/ChunkedRingBuffer.hpp
        src/audio/WinCapture.hpp
        src/audio/WinAudioSource.ixx
        src/audio/AudioSink.ixx
        src/audio/RingBuffer.ixx
)
target_include_directories(recorder PRIVATE src)
target_sources(recorder
  PUBLIC
    FILE_SET all_my_modules TYPE CXX_MODULES FILES
        src/audio/RingBuffer.ixx
        src/audio/AudioSource.ixx
        src/audio/WinAudioSource.ixx
)

target_link_libraries(recorder WIL::WIL spdlog::spdlog hmac_sha256 Opus::opus Ogg::ogg)

target_link_libraries(recorder
        mmdevapi.lib
        wbemuuid.lib
)

project(recorder-tests)
add_executable(recorder-tests
        tests.cpp
)
target_sources(recorder-tests
  PUBLIC
    FILE_SET all_my_modules TYPE CXX_MODULES FILES
        src/audio/RingBuffer.ixx
)
target_link_libraries(recorder-tests GTest::gtest)
