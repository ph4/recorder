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
FetchContent_Declare(
        reflect-cpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
        GIT_TAG v0.16.0
)
FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)
FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.18.3
)

set(TOMLPLUSPLUS_INSTALL ON)

set(REFLECTCPP_USE_VCPKG OFF)
set(REFLECTCPP_TOML ON)
set(WIL_BUILD_TESTS OFF)
FetchContent_MakeAvailable(wil spdlog hmac_sha256 opus ogg googletest cpp-httplib reflect-cpp tomlplusplus)

set(CXX_SCAN_FOR_MODULES ON)
add_definitions(-DNOMINMAX)
add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
add_definitions(-DCPPHTTPLIB_STATIC) # Not sure if neeed


file(GLOB_RECURSE SOURCES "src/*.cpp" "src/**/*.cpp")
file(GLOB_RECURSE MODULES "src/*.ixx" "src/**/*.ixx")

message(STATUS "Collected files: ${SOURCES}")
message(STATUS "Collected modules: ${MODULES}")
add_executable(recorder
        ${SOURCES}
)
target_include_directories(recorder PRIVATE src)
target_sources(recorder
        PUBLIC FILE_SET all_my_modules TYPE CXX_MODULES FILES ${MODULES}
)

target_link_libraries(recorder WIL::WIL spdlog::spdlog hmac_sha256 Opus::opus Ogg::ogg httplib::httplib reflectcpp)

target_link_libraries(recorder
        mmdevapi.lib
        wbemuuid.lib
        winhttp.lib
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
