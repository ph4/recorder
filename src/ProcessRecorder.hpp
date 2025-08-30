#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

#include <spdlog/spdlog.h>
#include <rfl/enums.hpp>

#include "Controller.hpp"

#include "audio/audio_core.hpp"
#include "audio/OggOpusEncoder.hpp"
#include "audio/RingBuffer.hpp"
#include "audio/WasapiAudioSource.hpp"

#include "FileUploader.hpp"
#include "Models.hpp"
#include "audio/ActivityMonitor.hpp"

using recorder::audio::AudioFormat;
using recorder::audio::IAudioSource;
using recorder::audio::OggOpusEncoder;

using recorder::models::RecordMetadata;

using std::filesystem::path;

using namespace std::chrono;

namespace recorder {

enum class RecorderType {
    Wasapi,
    WasapiWhatsapp,
};

struct File {
    std::shared_ptr<std::ofstream> file_stream;
    OggOpusEncoder opus_encoder_;
    path file_path;
    time_point<system_clock> start_time;
};

using recorder::audio::IActivityListener;
using recorder::audio::IActivityMonitor;
using recorder::audio::IAudioSinkTyped;
template <typename S> class ProcessRecorder
    : public IAudioSinkTyped<S>
    , public IActivityListener<S> {
    struct MicSink : public IAudioSinkTyped<S> {
        ProcessRecorder<S> *const recorder_;
        MicSink(ProcessRecorder<S> *const recorder) : recorder_(recorder) {}

        void OnNewPacket(std::span<S> packet) override { recorder_->MicIn(packet); }
    };

    MicSink mic_sink_;

    std::shared_ptr<Controller> controller_;
    std::string name_;
    std::shared_ptr<FileUploader> uploader_;
    AudioFormat format_;

    std::unique_ptr<audio::ISignalActivityMonitor<S>> activity_monitor_ = nullptr;

    std::mutex write_mutex_{};
    std::unique_ptr<IAudioSource> mic_;
    std::unique_ptr<IAudioSource> process_;

    std::thread encode_thread_{};
    std::condition_variable encode_condition_;
    std::mutex encode_mutex_{};
    bool encode_cond_{false};

    std::thread stop_thread_{};

    std::optional<File> file_ = std::nullopt;
    InterleaveRingBuffer<S, 2, 480, 50> buffer_{};

    bool stopped_ = false;

    std::optional<std::string> metadata_ = std::nullopt;

public:
    bool IsRecording() { return file_ != std::nullopt; }
    bool IsStopped() { return stopped_; }

    void OnNewPacket(std::span<S> packet) override { this->ProcessIn(packet); }

    void OnActive(std::optional<std::string> metadata) override { this->StartRecording(metadata); }
    void OnInactive() override { this->FinishRecording(); }

    ProcessRecorder(
          const std::shared_ptr<Controller> &controller,
          std::string name,
          const std::shared_ptr<FileUploader> &uploader,
          AudioFormat format,
          uint32_t pid,
          RecorderType type
    )
        : controller_(controller),
          name_(std::move(name)),
          uploader_(uploader),
          format_(format),
          mic_sink_(this) {
        // TODO: Make max_silence configurable
        if (type == RecorderType::Wasapi) {
            activity_monitor_ =
                  std::make_unique<recorder::audio::ActivityMonitorSilence<S>>(this, 5, format);
            mic_ = std::make_unique<audio::windows::WasapiAudioSource<S>>(
                  format, &mic_sink_, 0, false
            );
            process_ =
                  std::make_unique<audio::windows::WasapiAudioSource<S>>(format, this, pid, true);
        } else if (type == RecorderType::WasapiWhatsapp) {
            activity_monitor_ =
                  std::make_unique<recorder::audio::ActivityMonitorWhatsapp<S>>(this, format, 5);
            mic_ = std::make_unique<audio::windows::WasapiAudioSource<S>>(
                  format, &mic_sink_, 0, false
            );
            process_ =
                  std::make_unique<audio::windows::WasapiAudioSource<S>>(format, this, 0, true);
        } else {
            throw std::runtime_error("Not implemented");
        }
        encode_thread_ = std::thread(std::bind(&ProcessRecorder::EncodeLoop, this));
    }

protected:
    void StartRecording(std::optional<std::string> metadata) {
        metadata_ = metadata;
        const auto zone = current_zone();
        zoned_time now{zone, time_point_cast<seconds>(system_clock::now())};
        const auto start_time = time_point_cast<seconds>(now.get_sys_time());
        const auto file_name =
              metadata
                    ? std::format("{:%Y-%m-%dT%H_%M_%S%z}@{}#{}.ogg", now, name_, metadata.value())
                    : std::format("{:%Y-%m-%dT%H_%M_%S%z}@{}.ogg", now, name_);
        SPDLOG_INFO("Starting recording {}", file_name);
        auto file_path = uploader_->root_path() / file_name;
        const auto fs = std::make_shared<std::ofstream>(
              file_path, std::ios::binary | std::ios::trunc | std::ios::out
        );
        file_.emplace(
              File{
                    .file_stream = fs,
                    .opus_encoder_ = std::move(OggOpusEncoder(
                          fs, AudioFormat{.channels = 2, .sampleRate = format_.sampleRate}, 32
                    )),
                    .file_path = file_path,
                    .start_time = start_time,
              }
        );
        if (auto res = file_->opus_encoder_.Init()) {
            SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
            throw std::runtime_error("Failed to initialize OggOpusWriter");
        }
    }

    void FinishRecording() {
        SPDLOG_INFO("Finishing recording {}", file_->file_path.string());
        // file_->opus_encoder_.Push(buffer_.remainder());
        buffer_.Clear();
        if (auto res = file_->opus_encoder_.Finalize()) {
            SPDLOG_ERROR("Failed to finalize writer: {}", res);
            throw std::runtime_error("Failed to finalize writer");
        }
        file_->file_stream->close();

        const auto started_ts = duration_cast<seconds>(file_->start_time.time_since_epoch());
        auto length = duration_cast<seconds>(system_clock::now() - file_->start_time);
        const auto metadata = RecordMetadata{
              .started = static_cast<uint64_t>(started_ts.count()), .length_seconds = length.count()
        };

        uploader_->UploadFile(UploadFile{.file_path = file_->file_path, .metadata = metadata});
        file_ = std::nullopt;
        auto [command_type] =
              controller_->SetStatus(name_, InternalStatusBase(InternalStatusType::idle));
    }

    void MicIn(std::span<S> data) {
        if (!file_) {
            PostIdle();
            return;
        }
        auto can_push = buffer_.template CanPushSamples<0>();
        if (data.size() > can_push) {
            buffer_.PushChannel<0>(data.subspan(0, can_push));
            SPDLOG_WARN("MicIn buffer overflow: {}", data.size());
        } else {
            buffer_.PushChannel<0>(data);
        }
        PostWrite();
    }

    void ProcessIn(std::span<S> data) {
        if (activity_monitor_) {
            activity_monitor_->OnNewPacket(data);
        }
        if (!file_) {
            PostIdle();
            return;
        }
        auto can_push = buffer_.template CanPushSamples<1>();
        if (data.size() > can_push) {
            buffer_.PushChannel<1>(data.subspan(0, can_push));
            SPDLOG_WARN("ProcessIn buffer overflow: {}", data.size());
        } else {
            buffer_.PushChannel<1>(data);
        }
        PostWrite();
    }
    void PostIdle() {
        // Notify encode because it handles status as well
        {
            std::lock_guard guard(encode_mutex_);
            encode_cond_ = true;
        }
        encode_condition_.notify_one();
    };

    void PostWrite() {
        // Notify encode thread that it has work to do
        {
            std::lock_guard guard(encode_mutex_);
            encode_cond_ = true;
        }
        encode_condition_.notify_one();
    }

    void EncodeLoop() {
        while (true) {
            std::unique_lock lock(encode_mutex_);
            encode_condition_.wait(lock, [this] { return this->encode_cond_; });
            encode_cond_ = false;
            if (stopped_ == true) break;

            if (file_) {
                const auto started =
                      std::chrono::duration_cast<seconds>(file_->start_time.time_since_epoch())
                            .count();
                const auto current =
                      std::chrono::duration_cast<seconds>(system_clock::now().time_since_epoch())
                            .count();
                {
                    std::lock_guard guard(write_mutex_);
                    while (buffer_.HasChunks()) {
                        file_->opus_encoder_.Push(buffer_.Retrieve());
                    }
                }
                const auto md = RecordMetadata(started, current - started);
                auto [command_type] = controller_->SetStatus(
                      name_, InternalStatusWithMetadata(InternalStatusType::recording, md)
                );

                using enum models::CommandType;
                switch (command_type) {
                    case force_upload:
                        this->FinishRecording();
                        this->StartRecording(metadata_);
                        break;
                    case kill:
                    case stop:
                    case reload:
                    case normal:
                        break;
                    default: {
                        SPDLOG_ERROR("Unknown command type: {}", rfl::enum_to_string(command_type));
                        throw std::runtime_error("Unknown command type");
                    }
                }
            }
        }
    }

public:
    ~ProcessRecorder() {
        if (!stopped_) {
            if (file_) {
                this->FinishRecording();
            }
            stopped_ = true;
            mic_.reset();
            process_.reset();
            if (encode_thread_.joinable()) {
                PostWrite();
                encode_thread_.join();
            }
        }
    };
};

} // namespace recorder
