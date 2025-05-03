//
// Created by pavel on 16.12.2024.
//
#ifndef WIN_AUDIO_SOURCE_H
#define WIN_AUDIO_SOURCE_H

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "win-capture.hpp"

#include "AudioDeviceMonitor.hpp"
#include "AudioSource.hpp"
#include "SignalMonitor.hpp"

namespace recorder::audio::windows {
    using namespace recorder::audio;

    template <typename S> class WinAudioSource
        : public ProcessAudioSource<S>
        , SignalMonitorSimple {
        using State = typename ProcessAudioSource<S>::State;
        using DeviceState = typename WinCapture<S>::DeviceState;
        using CallBackT = typename ProcessAudioSource<S>::CallBackT;

        bool started_ = false;
        std::unique_ptr<AudioDeviceMonitor<S>> device_monitor = nullptr;
        std::mutex started_mutex_{};
        std::condition_variable started_condition_{};
        CallBackT callback_{};
        SignalMonitorSilenceCallbacks signal_callbacks_;
        size_t samples_without_signal_ = std::numeric_limits<size_t>::max();
        bool active_ = false;

        std::unique_ptr<WinCapture<S>> capture_;

        bool checkNewAudio(std::span<S> audio) {
            // Windows sometimes has samples with value of +-1 on silent streams
            auto has_signal =
                  std::any_of(audio.begin(), audio.end(), [&](auto s) { return std::abs(s) > 1; });
            if (active_ && has_signal) {
                samples_without_signal_ = 0;
            } else if (!active_ && has_signal) {
                active_ = true;
                samples_without_signal_ = 0;
                const SignalActiveData data{
                      .timestamp = std::chrono::system_clock::now(),
                      .activationSource = std::string("silence"),
                      .metadata = std::nullopt,
                };
                this->signal_callbacks_.onActivated(data);
            } else if (active_ && !has_signal) {
                samples_without_signal_ += audio.size();
                const size_t samples_per_sec =
                      capture_->GetFormat()->nSamplesPerSec * capture_->GetFormat()->nChannels;
                if (samples_without_signal_
                    > samples_per_sec * signal_callbacks_.max_silence_seconds) {
                    active_ = false;
                    this->signal_callbacks_.onDeactivated(SignalInactiveData{});
                }
            }
            return active_;
        };
        using ProcessAudioSource<S>::ProcessAudioSource;

      public:
        explicit WinAudioSource(
              AudioFormat format,
              typename AudioSource<S>::CallBackT callback,
              const SignalMonitorSilenceCallbacks &signal_callbacks,
              uint32_t pid,
              bool loopback
        )
            : ProcessAudioSource<S>(),
              callback_(callback),
              signal_callbacks_(signal_callbacks),
              capture_(
                    std::make_unique<WinCapture<S>>(
                          format,
                          200000,
                          [this](auto audio) {
                              this->checkNewAudio(audio);
                              callback_(audio);
                          },
                          pid,
                          loopback
                    )
              ) {
            if (!loopback && pid != 0) {
                throw std::invalid_argument("pid is invalid");
            }
            if (loopback) {
                device_monitor = std::make_unique<AudioDeviceMonitor<S>>(format, callback);
            }
        }

        State GetState() override {
            if (capture_) {
                if (capture_->GetError())
                    return State::Error;
                else
                    returrn State::Capturing;
            } else {
                return State::Stopped;
            }
        }

        void SetCallback(const CallBackT cb) override {
            throw std::runtime_error("Not implemented");
        };

        void Play() override {};

        void Stop() override {
            if (!started_) return;
            if (device_monitor) {
                device_monitor.release();
            }
            if (capture_) {
                capture_.release();
            }
        }

        uint32_t GetPid() override { throw std::runtime_error("Not implemented"); }

        bool HasSignal() override { return active_; };

        ~WinAudioSource() override {}
    };

    template class WinAudioSource<int16_t>;

} // namespace recorder::audio::windows
#endif
