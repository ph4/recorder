#include <iostream>
#include <string>
#include <semaphore>
#include <thread>
#include <span>
#include <fstream>

//Force include before Windows so it does not complain
#include <httplib.h>

// Workaround unresolved external symbol
#include <wrl.h>

#include <yyjson.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>

#include "hwid.hpp"
#include <rfl.hpp>
#include <rfl/Timestamp.hpp>
#include <rfl/toml/load.hpp>

import AudioSource;
import WinAudioSource;
import RecordManager;
import Models;
import Api;
import FileUploader;

using recorder::models::LocalConfig;

using std::chrono::system_clock;

#define FORMAT sizeof(uint16_t)

uint32_t total_write_ms = 5000;

std::binary_semaphore write_complete{0};

const std::string config_path = "config.toml";

int main(const int argc, char const *argv[]) {
    const auto logger = spdlog::stdout_color_mt("main");
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    set_default_logger(logger);

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    auto config_load = rfl::toml::load<LocalConfig>(config_path)
            .and_then([](auto config) { return rfl::Result(std::make_shared<LocalConfig>(std::move(config))); });
    if (!config_load) {
        auto a = config_load.error().value();
        SPDLOG_ERROR("Error reading config ({})", config_load.error().value().what());
        return 1;
    }
    auto config = config_load.value();
    auto api = std::make_shared<recorder::Api>(config);
    api->Register().or_else([](auto e) {
        SPDLOG_ERROR("Error registering API ({})", e.what());
        return e;
    });


    const uint32_t pid = argc > 1 ? std::stoi(argv[1]) : 0;
    total_write_ms = argc > 2 ? std::stoi(argv[2]) : 1000;
    SPDLOG_INFO("Pid = {}", pid);
    SPDLOG_INFO("total_write_ms = {}", total_write_ms);

    auto fc = recorder::audio::AudioFormat{
        .channels = 1,
        .sampleRate = 16000,
    };

    auto data_callback = [&](std::span<int16_t> data) {
    };

    auto uploader = std::make_shared<recorder::FileUploader>(api, std::filesystem::path("./records/"));
    auto recorder = recorder::ProcessRecorder<int16_t>(
            "main", uploader, fc, pid, [](auto fmt, auto cb, auto scb, auto pid, auto lb) {
                return std::make_unique<recorder::audio::windows::WinAudioSource<int16_t>>(fmt, cb, scb, pid, lb);
            });


    recorder.Play();
    std::this_thread::sleep_for(std::chrono::milliseconds(total_write_ms));
    recorder.Stop();

    return 0;
}
