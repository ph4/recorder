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


#include <controller.hpp>
#include <ranges>

#include <rfl.hpp>
#include <rfl/Timestamp.hpp>
#include <rfl/toml/load.hpp>
#include "hwid.hpp"
#include "logging.hpp"

#include "rfl/toml/save.hpp"

import AudioSource;
import WinAudioSource;
import ProcessRecorder;
import Models;
import Api;
import FileUploader;
import ProcessLister;

using recorder::models::LocalConfig;

using namespace std::chrono;

#define FORMAT sizeof(uint16_t)

uint32_t total_write_ms = 5000;

std::binary_semaphore write_complete{0};

const std::string config_path = "config.toml";

int main(const int argc, char const *argv[]) {
    setup_logger();


    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    auto config_load = rfl::toml::load<LocalConfig>(config_path)
            .and_then([](auto config) { return rfl::Result(std::make_shared<LocalConfig>(std::move(config))); });
    if (!config_load) {
        auto a = config_load.error().value();
        SPDLOG_ERROR("Error reading config ({})", config_load.error().value().what());
        return 1;
    }
    auto local_config = config_load.value();
    auto api = std::make_shared<recorder::Api>(local_config);
    auto res = api->Register().or_else([](auto e) {
        SPDLOG_ERROR("Error registering API ({})", e.what());
        return e;
    });


    auto remote_config_r = res.or_else([](auto e) {
        return rfl::toml::load<recorder::models::RemoteConfig>("remote_config.toml");
    });
    if (!remote_config_r) {
        SPDLOG_ERROR("Error loading remote config");
        return 1;
    }
    const auto remote_config = remote_config_r.value();
    rfl::toml::save<>("remote_config.toml", remote_config);

    const uint32_t pid = argc > 1 ? std::stoi(argv[1]) : 0;
    total_write_ms = argc > 2 ? std::stoi(argv[2]) : 1000;
    SPDLOG_INFO("Pid = {}", pid);
    SPDLOG_INFO("total_write_ms = {}", total_write_ms);

    auto fc = recorder::audio::AudioFormat{
        .channels = 1,
        .sampleRate = 16000,
    };

    const auto uploader = std::make_shared<recorder::FileUploader>(api, std::filesystem::path("./records/"));
    const auto controller = std::make_shared<recorder::Controller>(api, 5000);
    auto thread = std::thread([&]() {
        recorder::ProcessLister pl;
        std::unordered_map<std::string, std::unique_ptr<recorder::ProcessRecorder<int16_t>>> recorders;
        while (true) {
            auto start = high_resolution_clock::now();

            auto playing_processes = pl.getAudioPlayingProcesses();
            std::unordered_set<std::string> blacklist = {"explorer.exe"};
            auto new_processes = playing_processes | std::ranges::views::filter([&](recorder::ProcessInfo e) {
                        return !blacklist.contains(e.process_name()) && !recorders.contains(e.process_name());
                    });
            auto factory = [](auto fmt, auto cb, auto scb, auto pid, auto lb) {
                return std::make_unique<recorder::audio::windows::WinAudioSource<int16_t>>(fmt, cb, scb, pid, lb);
            };
            for (const auto &p: new_processes) {
                auto recorder = std::make_unique<recorder::ProcessRecorder<int16_t>>(
                        controller, p.process_name(), uploader, fc, p.process_id(), factory);
                SPDLOG_INFO("Starting recording on {}", p.process_name());
                recorder->Play();
                //TODO stoppage
                recorders.emplace(p.process_name(), std::move(recorder));
            }
            auto elapsed = high_resolution_clock::now() - start;
            auto to_sleep = milliseconds(200) - elapsed;
            std::this_thread::sleep_for(to_sleep);
        }
    });


    std::this_thread::sleep_for(milliseconds(total_write_ms));
    std::cout << "Goodbye!" << std::endl;

    return 0;
}
