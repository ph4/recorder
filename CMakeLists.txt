cmake_minimum_required(VERSION 3.29)
project(recorder)

set(CMAKE_CXX_STANDARD 20)

add_executable(recorder main.cpp WinCapture.cpp
)
target_include_directories(recorder PRIVATE wil/include)

target_link_libraries(recorder
        mmdevapi.lib
)

