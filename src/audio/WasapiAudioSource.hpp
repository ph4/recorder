//
// Created by pavel on 16.12.2024.
//
#ifndef WIN_AUDIO_SOURCE_H
#define WIN_AUDIO_SOURCE_H

#include <chrono>
#include <memory>

#include "WasapiCapture.hpp"

#include "AudioDeviceMonitor.hpp"
#include "AudioSource.hpp"

namespace recorder::audio::windows {
using namespace recorder::audio;
using namespace std::chrono;

template <typename S> class WasapiAudioSource
    : public ProcessAudioSource<S>
    , public IAudioSink<S> {
    std::unique_ptr<InactiveAudioDeviceHandler<S>> inactive_device_handler_ = nullptr;
    IAudioSink<S> *sink_;
    AudioFormat format_;

    std::mutex time_mutex_;
    steady_clock::time_point last_signal_ = steady_clock::now();
    std::thread no_signal_thread_;
    bool active_ = true;

    std::unique_ptr<WasapiCapture<S>> capture_;
    using ProcessAudioSource<S>::ProcessAudioSource;

public:
    explicit WasapiAudioSource(AudioFormat format, IAudioSink<S> *sink, uint32_t pid, bool loopback)
        : ProcessAudioSource<S>(),
          sink_(sink),
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

    void SetCallback(const ProcessAudioSource<S>::CallBackT cb) override {
        throw std::runtime_error("Not implemented");
    };

    uint32_t GetPid() override { throw std::runtime_error("Not implemented"); }

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
#endif
