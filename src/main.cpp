#include <fstream>
#include <iostream>
#include <semaphore>
#include <span>
#include <string>
#include <thread>

// Force include before Windows so it does not complain
#include <httplib.h>
#include <windows.h>

#include <filesystem>

#include "hwid.hpp"
#include "logging.hpp"

import Velopack;
import Recorder;

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

int main(const int argc, char const *argv[]) {
#ifndef DEBUG
    SetWorkdirToParent();
#endif
    ShowWindow(GetConsoleWindow(), SW_HIDE); // Hide terminal
    setup_logger();
    try {
        if (const auto res = recorder::velopack::init_velopack()) {
            return res;
        };

    } catch (const std::exception &e) {
        SPDLOG_ERROR("Exception occurred while starting velopack: {}", e.what());
    }

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    auto recorder = recorder::Recorder();
    std::thread recorder_thread([&] {
        try {
            recorder.MainLoop();
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Exception: {}", e.what());
        }
    });

    recorder_thread.join();


    std::cout << "Goodbye!" << std::endl;

    return 0;
}
