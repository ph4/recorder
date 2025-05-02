#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

// Force include before Windows so it does not complain
#include <httplib.h>
#include <windows.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>
#include <spdlog/spdlog.h>

#ifdef DEBUG
#include "debug.hpp"
#else
#include "VelopackMy.hpp"
#endif

#include "Recorder.hpp"
#include "hwid.hpp"

std::string app_path{"."};

void SetWorkdirToParent() {
    // Get the full path to the running executable
    char path[MAX_PATH];
    if (GetModuleFileName(NULL, path, MAX_PATH)) { // GetModuleFileNameA for char-based version
        // Get the directory from the full path
        auto directory = std::filesystem::path(path).parent_path().parent_path();

        // Set the current working directory to the executable's directory
        if (!SetCurrentDirectoryW(directory.c_str())) { // SetCurrentDirectoryW for wide string
            throw std::runtime_error("Failed to set working directory");
        }

        std::cout << "Current working directory set to: " << directory << std::endl;
    } else {
        throw std::runtime_error("Failed to get executable path");
    }
}

void setup_logger() {
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          "logs/main.txt", 1024 * 1024 * 5, 5, true
    );
    spdlog::logger logger("main", {stdout_sink, file_sink});
    logger.flush_on(spdlog::level::trace);
    logger.set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s] [%^%l%$] %v");
    logger.set_level(spdlog::level::trace);
    set_default_logger(std::make_shared<spdlog::logger>(logger));
}

int main(const int argc, char const *argv[]) {
#ifndef DEBUG
    SetWorkdirToParent();
    ShowWindow(GetConsoleWindow(), SW_HIDE); // Hide terminal
    setup_logger();
    try {
        if (const auto res = recorder::velopack::init_velopack()) {
            return res;
        };

    } catch (const std::exception &e) {
        SPDLOG_ERROR("Exception occurred while starting velopack: {}", e.what());
    }
#else
    setup_logger();
    recorder::debug::print_all_endpoints();
#endif

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    std::thread recorder_thread([&] {
        try {
            SPDLOG_INFO("Starting MainLoop...");
            while (true) {
                auto recorder = recorder::Recorder();
                recorder.Init();
                if (recorder.ListenProcesses()) {
                    SPDLOG_INFO("Reloading...");
                } else {
                    SPDLOG_INFO("Exiting...");
                    break;
                }
            }
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Main loop exception: {}", e.what());
        }
    });

    recorder_thread.join();

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
