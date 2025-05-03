//
// Created by pavel on 16.12.2024.
//
#ifndef WIN_AUDIO_SOURCE_H
#define WIN_AUDIO_SOURCE_H

#include <algorithm>
#include <memory>

#include "WasapiCapture.hpp"

#include "AudioDeviceMonitor.hpp"
#include "AudioSource.hpp"

namespace recorder::audio::windows {
    using namespace recorder::audio;
    template <typename S> class ActivityMonitorSilence : public IBasicListener<S> {
        size_t frames_silent_ = std::numeric_limits<size_t>::max();
        bool active_ = false;
        int max_silence_seconds_;
        const audio::AudioFormat format_;
        IListener<S> *const listener_ = nullptr;
        const size_t samples_per_sec = format_.sampleRate * format_.channels;

      public:
        ActivityMonitorSilence(
              IListener<S> *listener, int max_silence_seconds, audio::AudioFormat format
        )
            : listener_(listener), max_silence_seconds_(max_silence_seconds), format_(format) {}
        void OnNewPacket(std::span<S> packet) override {
            // Windows sometimes has samples with value of +-1 on silent streams
            auto has_signal = std::any_of(packet.begin(), packet.end(), [&](auto s) {
                return std::abs(s) > 1;
            });
            if (active_ && has_signal) {
                frames_silent_ = 0;
            } else if (!active_ && has_signal) {
                SPDLOG_DEBUG("+Active");
                active_ = true;
                frames_silent_ = 0;
                listener_->OnActive();
            } else if (active_ && !has_signal) {
                frames_silent_ += packet.size();
                if (frames_silent_ > samples_per_sec * max_silence_seconds_) {
                    SPDLOG_DEBUG("-Active");
                    active_ = false;
                    listener_->OnInactive();
                }
            }
            listener_->OnNewPacket(packet);
        };
    };

    template <typename S> class WasapiAudioSource : public ProcessAudioSource<S> {
        std::unique_ptr<AudioDeviceMonitor<S>> device_monitor_ = nullptr;
        ActivityMonitorSilence<S> activity_monitor_;
        const IListener<S> *listener_;

        std::unique_ptr<WasapiCapture<S>> capture_;
        using ProcessAudioSource<S>::ProcessAudioSource;

      public:
        explicit WasapiAudioSource(
              AudioFormat format,
              // typename AudioSource<S>::CallBackT callback,
              IListener<S> *listener,
              uint32_t pid,
              bool loopback,
              int max_silence_seconds
        )
            : ProcessAudioSource<S>(),
              listener_(listener),
              activity_monitor_(listener, max_silence_seconds, format),
              capture_(
                    std::make_unique<WasapiCapture<S>>(
                          format, 200000, &activity_monitor_, pid, loopback
                    )
              ) {
            if (!loopback && pid != 0) {
                throw std::invalid_argument("pid is invalid");
            }
            if (loopback) {
                device_monitor_ = std::make_unique<AudioDeviceMonitor<S>>(format, listener);
            }
        }

        using State = ProcessAudioSource<S>::State;
        State GetState() override {
            if (capture_) {
                if (capture_->GetError())
                    return State::Error;
                else
                    return State::Playing;
            } else {
                return State::Stopped;
            }
        }

        void SetCallback(const ProcessAudioSource<S>::CallBackT cb) override {
            throw std::runtime_error("Not implemented");
        };

        void Play() override {};

        void Stop() override {
            if (device_monitor_) {
                device_monitor_.release();
            }
            if (capture_) {
                capture_.release();
            }
        }

        uint32_t GetPid() override { throw std::runtime_error("Not implemented"); }

        ~WasapiAudioSource() override {}
    };

    template class WasapiAudioSource<int16_t>;

} // namespace recorder::audio::windows
#endif
