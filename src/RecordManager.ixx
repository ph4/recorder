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

using recorder::audio::ProcessAudioSource;
using recorder::audio::AudioFormat;
using recorder::audio::OggOpusEncoder;

export template<typename S>
class RecordManager {
public:
    RecordManager(const std::string &name, AudioFormat format,
                  std::unique_ptr<ProcessAudioSource<S> > mic,
                  std::unique_ptr<ProcessAudioSource<S> > process,
                  size_t total_write_ms)
      : total_write_ms_(total_write_ms),
        format_(format),
        name_(name),
        mic_(std::move(mic)),
        process_(std::move(process)),
        writer_(std::make_shared<std::ofstream>(std::format("{}.ogg", name),
                                                std::ios::binary | std::ios::trunc | std::ios::out)),
        opus_encoder_(
          std::move(OggOpusEncoder(
            writer_,
            AudioFormat{.channels = 2, .sampleRate = format_.sampleRate},
            32))) {
      if (auto res = opus_encoder_.Init()) {
        SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
        throw std::runtime_error("Failed to initialize OggOpusWriter");
      }

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

    std::binary_semaphore &wire_complete() {
      return this->write_complete_;
    }

protected:
    void MicIn(std::span<S> data) {
      auto can_push = buffer_.template CanPushSamples<0>();
      if (data.size() > can_push) {
        buffer_.PushChannel < 0 > (data.subspan(0, can_push));
        SPDLOG_WARN("MicIn buffer overflow: {}", data.size());
      } else {
        buffer_.PushChannel < 0 > (data);
      }
      WriteAudio();
    }

    void ProcessIn(std::span<S> data) {
      auto can_push = buffer_.template CanPushSamples<1>();
      if (data.size() > can_push) {
        buffer_.PushChannel < 1 > (data.subspan(0, can_push));
        SPDLOG_WARN("ProcessIn buffer overflow: {}", data.size());
      } else {
        buffer_.PushChannel < 1 > (data);
      }
      WriteAudio();
    }

    void WriteAudio() {
      std::lock_guard guard(write_mutex_);
      if (total_write_ms_ <= 0) return;

      while (buffer_.HasChunks() && total_write_ms_ > 0) {
        opus_encoder_.Push(buffer_.Retrieve());
        total_write_ms_ -= buffer_.chunk_frames() * 1000 / format_.sampleRate;
      }

      if (total_write_ms_ <= 0) {
        auto res = opus_encoder_.Finalize();
        if (res) {
          SPDLOG_ERROR("Failed to finalize writer: {}", res);
          throw std::runtime_error("Failed to finalize writer");
        } else {
          writer_->close();
        }
        write_complete_.release();
      }
    }

  private:
    std::mutex write_mutex_{};
    std::binary_semaphore write_complete_{0};
    int32_t total_write_ms_ = 5000;
    AudioFormat format_;
    std::string name_;
    std::unique_ptr<ProcessAudioSource<S> > mic_;
    std::unique_ptr<ProcessAudioSource<S> > process_;
    std::shared_ptr<std::ofstream> writer_;
    OggOpusEncoder opus_encoder_;
    InterleaveRingBuffer<S, 2, 480, 10> buffer_{};
  };
