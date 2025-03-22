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

#include <shlobj_core.h>

#include <yyjson.h>


#include <controller.hpp>
#include <ranges>

#include <rfl.hpp>
#include <rfl/Variant.hpp>
#include <rfl/Timestamp.hpp>
#include <rfl/toml/load.hpp>
#include <rfl/toml/save.hpp>


#include "hwid.hpp"
#include "logging.hpp"


import AudioSource;
import WinAudioSource;
import ProcessRecorder;
import Models;
import Api;
import FileUploader;
import ProcessLister;
import Velopack;

using recorder::models::LocalConfig;

using namespace std::chrono;
namespace rv = std::ranges::views;

#define FORMAT sizeof(uint16_t)

steady_clock::time_point last_update_time{};


struct RecorderItem {
    std::unique_ptr<recorder::ProcessRecorder<int16_t>> recorder;
    recorder::ProcessInfo process;
};

std::shared_ptr<LocalConfig> local_config;
std::string app_path{"."};

int start() {
    auto config_path = app_path + "\\config.toml";
    auto config_load = rfl::toml::load<LocalConfig>(config_path).and_then([](auto config) {
        return rfl::Result(std::make_shared<LocalConfig>(std::move(config)));
    });
    if (!config_load) {
        auto a = config_load.error().value();
        SPDLOG_ERROR("Error reading config ({})", config_load.error().value().what());
        return 1;
    }
    local_config = config_load.value();
    auto api = std::make_shared<recorder::Api>(local_config);
    auto res = api->Authorize().or_else([](auto e) {
        SPDLOG_ERROR("Error authorizing API ({})", e.what());
        return e;
    });


    auto api_a = api->EnsureAuthorized();

    auto set_name_r = api->SetName();
    if (set_name_r.error()) {
        SPDLOG_ERROR("Error setting name {}", set_name_r.error().value().what());
    }

    auto remote_cfg = api->GetConfig();
    auto using_cached_remote_config = false;
    auto remote_config_r = remote_cfg.or_else([&](auto e) {
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

    const auto uploader = std::make_shared<recorder::FileUploader>(api, std::filesystem::path(app_path + "/records/"));
    const auto controller = std::make_shared<recorder::Controller>(api, 5000);
    // controller->SetStatus("main", recorder::InternalStatusBase{.type = recorder::InternalStatusType::idle});

    auto audio_format = recorder::audio::AudioFormat{
            .channels = 1,
            .sampleRate = 16000,
    };

    recorder::ProcessLister pl;
    std::unordered_map<std::string, std::unique_ptr<RecorderItem>> recorders;
    while (true) {
        auto start = high_resolution_clock::now();
        switch (auto cmd = controller->GetGlobalCommand().type) {
            using enum recorder::models::CommandType;
            case reload:
            case stop:
            case kill: {
                bool all_stopped = true;
                for (auto &recorder: recorders | std::views::values) {
                    auto &rec = recorder->recorder;
                    if (!rec->IsStopped() && rec->IsStarted()) {
                        all_stopped = false;
                    }
                }
                if (all_stopped) {
                    if (cmd == reload) {
                        return 0x0EADBEEF;
                    } else {
                        return 0;
                    }
                } else {
                    auto elapsed = high_resolution_clock::now() - start;
                    auto to_sleep = milliseconds(200) - elapsed;
                    std::this_thread::sleep_for(to_sleep);
                    continue;
                }
                break;
            }
            default:
                break;
        }

        auto playing_processes = pl.getAudioPlayingProcesses();
        std::unordered_set<std::string> whitelist;
        auto wl = remote_config.app_configs | rv::transform([](recorder::models::App app) { return app.exe_name; });
        for (auto e: wl) {
            whitelist.insert(e);
        }
        auto new_processes = playing_processes | rv::filter([&](recorder::ProcessInfo e) {
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
stop_loop: {}
}


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
    if (const auto res = recorder::velopack::init_velopack()) {
        return res;
    };

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    std::thread recorder_thread([&] {
        while (true) {
            const auto res = start();
            if (res == 0x0EADBEEF) {
                SPDLOG_INFO("Reloading");
                // RELOAD
            } else if (res != 0) {
                std::exit(res);
            } else {
                break;
            }
        }
    });

    recorder_thread.join();


    std::cout << "Goodbye!" << std::endl;

    return 0;
}
