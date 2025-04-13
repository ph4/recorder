//
// Created by pavel on 23.12.2024.
//
#ifndef PROCESSRECORDER_HPP
#define PROCESSRECORDER_HPP

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

#include <rfl/enums.hpp>

#include "Controller.hpp"
#include "logging.hpp"

#include "audio/AudioSource.hpp"
#include "audio/RingBuffer.hpp"
#include "audio/SignalMonitor.hpp"
#include "audio/OggOpusEncoder.hpp"

#include "FileUploader.hpp"
#include "Models.hpp"

using recorder::audio::AudioFormat;
using recorder::audio::OggOpusEncoder;
using recorder::audio::ProcessAudioSource;

using recorder::models::RecordMetadata;

using std::filesystem::path;

using namespace std::chrono;

namespace recorder {
    struct File {
        std::shared_ptr<std::ofstream> file_stream;
        OggOpusEncoder opus_encoder_;
        path file_path;
        time_point<system_clock> start_time;
    };

    template<typename S>
    class ProcessRecorder {
        std::shared_ptr<Controller> controller_;
        std::string name_;
        std::shared_ptr<FileUploader> uploader_;
        AudioFormat format_;

        std::mutex write_mutex_{};
        std::unique_ptr<ProcessAudioSource<S>> mic_;
        std::unique_ptr<ProcessAudioSource<S>> process_;

        std::thread encode_thread_{};
        std::condition_variable encode_condition_;
        std::mutex encode_mutex_{};
        bool encode_cond_{false};

        std::thread stop_thread_{};

        std::optional<File> file_ = std::nullopt;
        InterleaveRingBuffer<S, 2, 480, 50> buffer_{};

        bool started_ = false;
        bool stopped_ = false;
    public:
        bool IsStarted() {
            return started_;
        }
        bool IsRecording() {
            return file_ != std::nullopt;
        }
        bool IsStopped() {
            return stopped_;
        }

    protected:
        void StartRecording(audio::SignalActiveData dat) {
            const auto zone = current_zone();
            zoned_time now{zone, std::chrono::time_point_cast<seconds>(system_clock::now())};
            const auto start_time = std::chrono::time_point_cast<seconds>(now.get_sys_time());
            const auto file_name = std::format("{:%Y-%m-%dT%H_%M_%S%z}@{}.ogg", now, name_);
            SPDLOG_INFO("Starting recording {}", file_name);
            auto file_path = uploader_->root_path() / file_name;
            const auto fs =
                    std::make_shared<std::ofstream>(file_path, std::ios::binary | std::ios::trunc | std::ios::out);
            file_.emplace(File{
                    .file_stream = fs,
                    .opus_encoder_ = std::move(
                            OggOpusEncoder(fs, AudioFormat{.channels = 2, .sampleRate = format_.sampleRate}, 32)),
                    .file_path = file_path,
                    .start_time = start_time,
            });
            if (auto res = file_->opus_encoder_.Init()) {
                SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
                throw std::runtime_error("Failed to initialize OggOpusWriter");
            }
        }

        void FinishRecording(audio::SignalInactiveData dat) {
            SPDLOG_INFO("Finishing recording {}", file_->file_path.string());
            // file_->opus_encoder_.Push(buffer_.remainder());
            buffer_.Clear();
            if (auto res = file_->opus_encoder_.Finalize()) {
                SPDLOG_ERROR("Failed to finalize writer: {}", res);
                throw std::runtime_error("Failed to finalize writer");
            }
            file_->file_stream->close();

            const auto started = std::chrono::duration_cast<seconds>(file_->start_time.time_since_epoch()).count();
            const auto current = std::chrono::duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            const auto metadata =
                    RecordMetadata{.started = static_cast<uint64_t>(started), .length_seconds = current - started};

            uploader_->UploadFile(UploadFile{.file_path = file_->file_path, .metadata = metadata});
            file_ = std::nullopt;
            auto [command_type] = controller_->SetStatus(name_, InternalStatusBase(InternalStatusType::idle));
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
            encode_cond_ = true;
            encode_condition_.notify_one();
        };

        void PostWrite() {
            // Notify encode thread that it has work to do
            encode_cond_ = true;
            encode_condition_.notify_one();
        }


        void EncodeLoop() {
            while (true) {
                std::unique_lock lock(encode_mutex_);
                encode_condition_.wait(lock, [this] { return this->encode_cond_; });
                encode_cond_ = false;
                if (stopped_ == true)
                    break;

                if (file_) {
                    const auto started =
                            std::chrono::duration_cast<seconds>(file_->start_time.time_since_epoch()).count();
                    const auto current =
                            std::chrono::duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                    {
                        std::lock_guard guard(write_mutex_);
                        while (buffer_.HasChunks()) {
                            file_->opus_encoder_.Push(buffer_.Retrieve());
                        }
                    }
                    const auto md = RecordMetadata(started, current - started);
                    auto [command_type] = controller_->SetStatus(
                            name_, InternalStatusWithMetadata(InternalStatusType::recording, md));

                    using enum models::CommandType;
                    switch (command_type) {
                        case force_upload:
                            this->FinishRecording(audio::SignalInactiveData{});
                            this->StartRecording(audio::SignalActiveData{
                                    .timestamp = system_clock::now(),
                                    .activationSource = "force_upload",
                            });
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
        using SourceFactory = std::function<std::unique_ptr<ProcessAudioSource<S>>(
                AudioFormat format,
                typename audio::AudioSource<S>::CallBackT callback,
                const audio::SignalMonitorSilenceCallbacks &callbacks,
                uint32_t pid,
                bool loopback)>;

        ProcessRecorder(const std::shared_ptr<Controller> &controller,
                        std::string name,
                        const std::shared_ptr<FileUploader> &uploader,
                        AudioFormat format,
                        uint32_t pid,
                        SourceFactory audio_source_factory)
            : controller_(controller), name_(std::move(name)), uploader_(uploader), format_(format) {
            auto callbacks = audio::SignalMonitorSilenceCallbacks(
                    [this](auto p) { StartRecording(p); }, [this](auto p) { FinishRecording(p); }, 4);
            auto callbacks_null = audio::SignalMonitorSilenceCallbacks(
                    [this](auto) {}, [this](auto) {}, std::numeric_limits<size_t>::max());
            mic_ = audio_source_factory(format, [this](auto p) { if (this->file_) MicIn(p); }, callbacks_null, 0, false);
            process_ = audio_source_factory(format, [this](auto p) { if (this->file_) ProcessIn(p); }, callbacks, pid, true);
            encode_thread_ = std::thread(std::bind(&ProcessRecorder::EncodeLoop, this));
        }


        void Play() {
            if (!started_) {
                mic_->Play();
                process_->Play();
                started_ = true;
            }
        }

        void Stop() {
            if (started_ && !stopped_) {
                if (file_) {
                    this->FinishRecording(audio::SignalInactiveData{});
                }
                stopped_ = true;
                mic_->Stop();
                process_->Stop();
                if (encode_thread_.joinable()) {
                    PostWrite();
                    encode_thread_.join();
                }
            }
        }
    };
} // namespace recorder
#endif //PROCESSRECORDER_HPP
