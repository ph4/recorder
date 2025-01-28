#include <fstream>
#include <iostream>
#include <semaphore>
#include <span>
#include <string>
#include <thread>

// Force include before Windows so it does not complain
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


std::binary_semaphore write_complete{0};

const std::string config_path = "config.toml";

struct RecorderItem {
    std::unique_ptr<recorder::ProcessRecorder<int16_t>> recorder;
    recorder::ProcessInfo process;
};

int start() {
    auto config_load = rfl::toml::load<LocalConfig>(config_path).and_then([](auto config) {
        return rfl::Result(std::make_shared<LocalConfig>(std::move(config)));
    });
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


    auto using_cached_remote_config = false;
    auto remote_config_r = res.or_else([&](auto e) {
        SPDLOG_WARN("Using cached remote config");
        using_cached_remote_config = true;
        return rfl::toml::load<recorder::models::RemoteConfig>("remote_config.toml");
    });
    if (!remote_config_r) {
        SPDLOG_ERROR("Error loading remote config");
        return 1;
    }
    const auto remote_config = remote_config_r.value();
    if (!using_cached_remote_config) {
        rfl::toml::save<>("remote_config.toml", remote_config);
    }

    const auto uploader = std::make_shared<recorder::FileUploader>(api, std::filesystem::path("./records/"));
    const auto controller = std::make_shared<recorder::Controller>(api, 5000);

    auto audio_format = recorder::audio::AudioFormat{
            .channels = 1,
            .sampleRate = 16000,
    };

    recorder::ProcessLister pl;
    std::unordered_map<std::string, std::unique_ptr<RecorderItem>> recorders;
    while (true) {
        auto start = high_resolution_clock::now();

        auto playing_processes = pl.getAudioPlayingProcesses();
        std::unordered_set<std::string> whitelist;
        auto wl = remote_config.app_configs |
                  std::ranges::views::transform([](recorder::models::App app) { return app.exe_name; });
        for (auto e: wl) {
            whitelist.insert(e);
        }
        auto new_processes = playing_processes | std::ranges::views::filter([&](recorder::ProcessInfo e) {
                                 return whitelist.contains(e.process_name()) && !recorders.contains(e.process_name());
                             });
        auto factory = [](auto fmt, auto cb, auto scb, auto pid, auto lb) {
            return std::make_unique<recorder::audio::windows::WinAudioSource<int16_t>>(fmt, cb, scb, pid, lb);
        };
        for (const auto &p: new_processes) {
            auto recorder = std::make_unique<recorder::ProcessRecorder<int16_t>>(
                    controller, p.process_name(), uploader, audio_format, p.process_id(), factory);
            SPDLOG_INFO("Starting recording on {}", p.process_name());
            recorder->Play();
            auto ri = std::make_unique<RecorderItem>(std::move(recorder), p);
            recorders.emplace(p.process_name(), std::move(ri));
        }
        auto to_remove = std::vector<std::string>();
        for (const auto &[k, v]: recorders) {
            if (!v->process.isAlive()) {
                SPDLOG_INFO("Stopping recording on {}", v->process.process_name());
                v->recorder->Stop();
                to_remove.push_back(k);
            }
        }
        for (const auto &k: to_remove) {
            recorders.erase(k);
        }
        auto elapsed = high_resolution_clock::now() - start;
        auto to_sleep = milliseconds(200) - elapsed;
        std::this_thread::sleep_for(to_sleep);
    }
}

int main(const int argc, char const *argv[]) {
    setup_logger();

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    std::thread recorder_thread([&] {
        const auto res = start();
        if (res != 0) {
            std::exit(res);
        }
    });

    recorder_thread.join();


    std::cout << "Goodbye!" << std::endl;

    return 0;
}
