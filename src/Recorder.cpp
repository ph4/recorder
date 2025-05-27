//
// Created by pavel on 22.03.2025.
//
#include "Recorder.hpp"

// Have to include it here so static functions compile
#include <strsafe.h>
#include <yyjson.h>

#include <ranges>

#include <spdlog/spdlog.h>
#include <rfl/toml/load.hpp>
#include <rfl/toml/save.hpp>

#include "Controller.hpp"
#include "Models.hpp"

using namespace std::chrono;
namespace rv = std::ranges::views;

namespace recorder {
void Recorder::LoadConfig() {
    auto config_path = ".\\config.toml";
    auto config_load = rfl::toml::load<models::LocalConfig>(config_path).and_then([](auto config) {
        return rfl::Result(std::make_shared<models::LocalConfig>(std::move(config)));
    });
    if (!config_load) {
        auto a = config_load.error().value();
        SPDLOG_ERROR("Error reading config ({})", config_load.error().value().what());
        throw std::runtime_error(config_load.error().value().what());
    }
    this->config_ = config_load.value();
}

void Recorder::Register() {
    SPDLOG_TRACE("api.Authorize()");
    auto res = api_->Authorize().or_else([](auto e) {
        SPDLOG_ERROR("Error authorizing API ({})", e.what());
        throw std::runtime_error("Error authorizing API ({})");
        return std::monostate{};
    });

    SPDLOG_TRACE("api.EnsureAuthorized()");
    auto api_a = api_->EnsureAuthorized();

    SPDLOG_TRACE("api.SetName()");
    auto set_name_r = api_->SetName();
    if (set_name_r.error()) {
        SPDLOG_ERROR("Error setting name {}", set_name_r.error().value().what());
    }

    SPDLOG_TRACE("api.GetConfig()");
    auto remote_cfg = api_->GetConfig();
    auto using_cached_remote_config = false;
    auto remote_config_r = remote_cfg.or_else([&](auto e) {
        SPDLOG_WARN("Using cached remote config");
        using_cached_remote_config = true;
        return rfl::toml::load<models::RemoteConfig>("remote_config.toml");
    });
    if (!remote_config_r) {
        SPDLOG_ERROR("Error loading remote config");
        throw std::runtime_error("Error loading remote config");
    }
    this->remote_config_ = remote_config_r.value();
    if (!using_cached_remote_config) {
        rfl::toml::save<>("remote_config.toml", this->remote_config_);
    }
}

void Recorder::StopAll() {
    SPDLOG_TRACE("Recorder::StopAll()");
    for (const auto &recorder : this->recorders_ | std::views::values) {
        const auto &rec = recorder->recorder;
        rec->Stop();
    }
}

void Recorder::StartListeningProcess(const ProcessInfo &pi) {
    SPDLOG_TRACE("Recorder::StartRecordingOnProcess()");
    auto audio_format = audio::AudioFormat{
          .channels = 1,
          .sampleRate = 16000,
    };

    // TODO: kostill
    auto type =
          pi.process_name() == "WhatsApp.exe" ? RecorderType::WasapiWhatsapp : RecorderType::Wasapi;

    auto recorder = std::make_unique<ProcessRecorder<int16_t>>(
          this->controller_, pi.process_name(), this->uploader_, audio_format, pi.process_id(), type
    );
    SPDLOG_INFO("Starting listening on {}", pi.process_name());
    recorder->Play();
    auto ri = std::make_unique<RecorderItem>(std::move(recorder), pi);
    this->recorders_.emplace(pi.process_name(), std::move(ri));
}

void Recorder::AddNewProcesses() {
    auto playing_processes = process_lister_.getAudioPlayingProcesses();
    auto new_processes = playing_processes | rv::filter([&](const ProcessInfo &pi) {
                             return app_whitelist_.contains(pi.process_name())
                                    && !recorders_.contains(pi.process_name());
                         });
    if (!new_processes.empty()) {
        SPDLOG_DEBUG("NewProcesses:");
    }
    for (const auto &p : new_processes) {
        SPDLOG_INFO("\t{} -> StartRecordingOnProcess", p.process_name());
        StartListeningProcess(p);
    }
}

void Recorder::RemoveStoppedProcesses() {
    auto to_remove = std::vector<std::string>();
    for (const auto &[k, v] : this->recorders_) {
        if (!v->process.isAlive()) {
            SPDLOG_INFO("Stopping recording on {}", v->process.process_name());
            v->recorder->Stop();
            to_remove.push_back(k);
        }
    }
    for (const auto &k : to_remove) {
        this->recorders_.erase(k);
    }
}

bool Recorder::ListenProcesses() {
    while (true) {
        auto start = high_resolution_clock::now();
        switch (auto cmd = this->controller_->GetGlobalCommand().type) {
            using enum models::CommandType;
            case reload:
            case stop:
            case kill: {
                StopAll();
                if (cmd == reload) {
                    return true;
                }
                return false;
            }
            default:;
        }
        AddNewProcesses();
        RemoveStoppedProcesses();

        auto elapsed = high_resolution_clock::now() - start;
        auto to_sleep = milliseconds(100) - elapsed;
        std::this_thread::sleep_for(to_sleep);
    }
}

void Recorder::Init() {
    SPDLOG_DEBUG("Loading config");
    this->LoadConfig();
    this->api_ = std::make_shared<Api>(this->config_);
    SPDLOG_DEBUG("Registering");
    this->Register();

    SPDLOG_TRACE("Creating uploader");
    this->uploader_ = std::make_shared<FileUploader>(api_, std::filesystem::path("./records/"));
    SPDLOG_TRACE("Creating controller");
    this->controller_ = std::make_shared<Controller>(api_, 5000);
    // controller->SetStatus("main", recorder::InternalStatusBase{.type =
    // recorder::InternalStatusType::idle});

    const auto wl = this->remote_config_.app_configs
                    | rv::transform([](models::App app) { return app.exe_name; });
    for (auto e : wl) {
        app_whitelist_.insert(e);
    }

    SPDLOG_DEBUG("App whitelist:");
    for (auto app : app_whitelist_) {
        SPDLOG_DEBUG("\t{}", app);
    }
}

} // namespace recorder
