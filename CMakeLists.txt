cmake_minimum_required(VERSION 3.29)
project(recorder)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
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

FetchContent_MakeAvailable(wil spdlog hmac_sha256)

add_executable(recorder
        src/main.cpp src/WinCapture.cpp src/hwid.hpp
)

target_link_libraries(recorder WIL::WIL spdlog::spdlog hmac_sha256)

target_link_libraries(recorder
        mmdevapi.lib
        wbemuuid.lib
)

