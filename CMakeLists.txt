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
FetchContent_MakeAvailable(wil spdlog)

add_executable(recorder main.cpp WinCapture.cpp
)

target_include_directories(recorder PRIVATE wil/include)

target_link_libraries(recorder WIL::WIL spdlog::spdlog )

target_link_libraries(recorder
        mmdevapi.lib
)

