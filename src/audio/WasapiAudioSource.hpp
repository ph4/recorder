#pragma once

#include <chrono>
#include <memory>

#include "audio_core.hpp"
#include "AudioDeviceMonitor.hpp"
#include "WasapiCapture.hpp"

namespace recorder::audio::windows {
using namespace recorder::audio;
using namespace std::chrono;

template <typename S> class WasapiAudioSource
    : public IAudioSource
    , public IAudioSinkTyped<S> {
    std::unique_ptr<InactiveAudioDeviceHandler<S>> inactive_device_handler_ = nullptr;
    IAudioSinkTyped<S> *sink_;
    AudioFormat format_;

    std::mutex time_mutex_;
    steady_clock::time_point last_signal_ = steady_clock::now();
    std::thread no_signal_thread_;
    bool active_ = true;

    std::unique_ptr<WasapiCapture<S>> capture_;

public:
    explicit WasapiAudioSource(
          AudioFormat format, IAudioSinkTyped<S> *sink, uint32_t pid, bool loopback
    )
        : sink_(sink),
          format_(format),
          capture_(std::make_unique<WasapiCapture<S>>(format, 200000, this, pid, loopback)) {
        if (!loopback && pid != 0) {
            throw std::invalid_argument("pid is invalid");
        }
        if (loopback) {
            inactive_device_handler_ =
                  std::make_unique<InactiveAudioDeviceHandler<S>>(format, sink);
            no_signal_thread_ = std::thread(&WasapiAudioSource::NoSignalWathcer, this);
        }
    }

    const AudioFormat &GetFormat() const override { return format_; }

    void NoSignalWathcer() {
        while (active_) {
            auto now = steady_clock::now();
            duration<double> delta;
            {
                std::lock_guard guard(time_mutex_);
                delta = now - last_signal_;
            }
            if (delta > std::chrono::milliseconds(200)) {
                auto milliseconds =
                      std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
                auto packet = std::vector<S>(
                      format_.channels * format_.sampleRate * 1000 / milliseconds, 0
                );
                sink_->OnNewPacket(std::span<S>(packet));
                {
                    std::lock_guard guard(time_mutex_);
                    last_signal_ = now;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void OnNewPacket(std::span<S> packet) override {
        sink_->OnNewPacket(packet);
        {
            std::lock_guard guard(time_mutex_);
            last_signal_ = std::chrono::steady_clock::now();
        }
    }

    ~WasapiAudioSource() override {
        if (inactive_device_handler_) {
            inactive_device_handler_.reset();
        }
        if (capture_) {
            capture_.reset();
        }
        active_ = false;
        if (no_signal_thread_.joinable()) {
            no_signal_thread_.join();
        }
    }
};

template class WasapiAudioSource<int16_t>;

} // namespace recorder::audio::windows
