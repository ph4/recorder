//
// Created by pavel on 23.12.2024.
//
module;
#include <fstream>
#include <memory>
#include <semaphore>
#include <span>

#include "spdlog/spdlog.h"

export module RecordManager;

import AudioSource;
import RingBuffer;
import OggOpusEncoder;
import FileUploader;
import Models;

using recorder::audio::AudioFormat;
using recorder::audio::OggOpusEncoder;
using recorder::audio::ProcessAudioSource;

using recorder::models::RecordMetadata;

using std::filesystem::path;

namespace recorder {
    export struct File {
        std::shared_ptr<std::ofstream> file_stream;
        OggOpusEncoder opus_encoder_;
        UploadFile upload_file;
    };

    export template<typename S>
    class RecordManager {
        std::string name_;
        std::shared_ptr<FileUploader> uploader_;
        AudioFormat format_;

        std::binary_semaphore write_complete_{0};
        int32_t total_write_ms_ = 5000;

        std::mutex write_mutex_{};
        std::unique_ptr<ProcessAudioSource<S>> mic_;
        std::unique_ptr<ProcessAudioSource<S>> process_;

        std::optional<File> file_ = std::nullopt;
        std::optional<OggOpusEncoder> opus_encoder_;
        InterleaveRingBuffer<S, 2, 480, 10> buffer_{};

    protected:
    public:
        void StartRecording(RecordMetadata metadata) {
            auto path = ::path(name_).replace_extension(::path(".ogg"));
            const auto fs = std::make_shared<std::ofstream>(path, std::ios::binary | std::ios::trunc | std::ios::out);
            file_.emplace(
                File{
                    .file_stream = fs,
                    .opus_encoder_ = std::move(OggOpusEncoder(
                            fs, AudioFormat{.channels = 2, .sampleRate = format_.sampleRate}, 32)),
                    .upload_file = {.file_path = path, .metadata = metadata},
            });
            if (auto res = file_->opus_encoder_.Init()) {
                SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
                throw std::runtime_error("Failed to initialize OggOpusWriter");
            }
        }
    protected:

        void FinishRecording() {
            file_->opus_encoder_.Push(buffer_.remainder());
            buffer_.Clear();
            if (auto res = file_->opus_encoder_.Finalize()) {
                SPDLOG_ERROR("Failed to finalize writer: {}", res);
                throw std::runtime_error("Failed to finalize writer");
            }
            file_->file_stream->close();
            uploader_->UploadFile(file_->upload_file);
            file_ = std::nullopt;
        }

        void MicIn(std::span<S> data) {
            if (!file_) return;
            auto can_push = buffer_.template CanPushSamples<0>();
            if (data.size() > can_push) {
                buffer_.PushChannel<0>(data.subspan(0, can_push));
                SPDLOG_WARN("MicIn buffer overflow: {}", data.size());
            } else {
                buffer_.PushChannel<0>(data);
            }
            WriteAudio();
        }

        void ProcessIn(std::span<S> data) {
            if (!file_) return;
            auto can_push = buffer_.template CanPushSamples<1>();
            if (data.size() > can_push) {
                buffer_.PushChannel<1>(data.subspan(0, can_push));
                SPDLOG_WARN("ProcessIn buffer overflow: {}", data.size());
            } else {
                buffer_.PushChannel<1>(data);
            }
            WriteAudio();
        }

        void WriteAudio() {
            if (!file_) return;
            std::lock_guard guard(write_mutex_);
            if (total_write_ms_ <= 0)
                return;

            while (buffer_.HasChunks() && total_write_ms_ > 0) {
                file_->opus_encoder_.Push(buffer_.Retrieve());
                total_write_ms_ -= buffer_.chunk_frames() * 1000 / format_.sampleRate;
            }

            if (total_write_ms_ <= 0) {
                FinishRecording();
                write_complete_.release();
            }
        }

    public:
        std::binary_semaphore &wire_complete() { return this->write_complete_; }

        RecordManager(const std::string &name, const std::shared_ptr<FileUploader> &uploader, AudioFormat format,
                      std::unique_ptr<ProcessAudioSource<S>> mic, std::unique_ptr<ProcessAudioSource<S>> process,
                      size_t total_write_ms)
            : name_(name),
              uploader_(uploader),
              format_(format),

              total_write_ms_(total_write_ms),

              mic_(std::move(mic)),
              process_(std::move(process))
        {


            mic_->SetCallback([this](auto data) { this->MicIn(data); });
            process_->SetCallback([this](auto data) { this->ProcessIn(data); });
        }



        void Play() {
            mic_->Play();
            process_->Play();
        }

        void Stop() {
            mic_->Stop();
            process_->Stop();
        }

    };
} // namespace recorder
