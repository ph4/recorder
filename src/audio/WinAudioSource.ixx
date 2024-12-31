//
// Created by pavel on 16.12.2024.
//
module;
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "WinCapture.hpp"

export module WinAudioSource;
import AudioSource;
import SignalMonitor;

namespace recorder::audio::windows {
    using namespace recorder::audio;

    export template<typename S>
    class WinAudioSource : public ProcessAudioSource<S>, SignalMonitorSimple {
        using State = typename ProcessAudioSource<S>::State;
        using DeviceState = typename WinCapture<S>::DeviceState;
        using CallBackT = typename ProcessAudioSource<S>::CallBackT;


        bool started_ = false;
        std::mutex started_mutex_{};
        std::condition_variable started_condition_{};
        CallBackT callback_{};
        SignalMonitorSilenceCallbacks signal_callbacks_;
        size_t samples_without_signal_ = std::numeric_limits<size_t>::max();
        bool active_ = false;

        wil::com_ptr<WinCapture<S>> capture_;
        std::thread thread_{};
        void process() {
            {
                std::unique_lock lock(started_mutex_);
                started_condition_.wait(lock, [this] { return started_; });
            }
            capture_->CaptureLoop();
        }

        bool checkNewAudio(std::span<S> audio) {
            // Windows sometimes has samples with value of +-1 on silent streams
            auto has_signal = std::any_of(audio.begin(), audio.end(), [&](auto s) { return std::abs(s) > 1; });
            if (active_ && has_signal) {
                samples_without_signal_ = 0;
            }
            else if (!active_ && has_signal) {
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
                const size_t samples_per_sec = capture_->GetFormat()->nSamplesPerSec * capture_->GetFormat()->nChannels;
                if (samples_without_signal_ > samples_per_sec * signal_callbacks_.max_silence_seconds ) {
                    active_ = false;
                    this->signal_callbacks_.onDeactivated(SignalInactiveData{});
                }
            }
            return active_;
        };
        using ProcessAudioSource<S>::ProcessAudioSource;
    public:
        explicit WinAudioSource(AudioFormat format,
                                typename AudioSource<S>::CallBackT callback,
                                const SignalMonitorSilenceCallbacks &signal_callbacks,
                                uint32_t pid,
                                bool loopback
                                )
            : ProcessAudioSource<S>(),
              callback_(callback),
              signal_callbacks_(signal_callbacks),
              capture_(Microsoft::WRL::Make<WinCapture<S>>(
                               format,
                               200000,
                               [this](auto audio) {
                                   this->checkNewAudio(audio);
                                   callback_(audio);
                               },
                               pid,
                               loopback)
                               .Detach()) {

            if (!loopback && pid != 0) {
                throw std::invalid_argument("pid is invalid");
            }

            THROW_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            THROW_IF_FAILED(capture_->Initialize());
            THROW_IF_FAILED(capture_->ActivateAudioInterface());
        }


        State GetState() override {
            switch (capture_->GetDeviceState()) {
                case DeviceState::Uninitialized: // NOLINT(*-branch-clone)
                    return State::Uninitialized;
                case DeviceState::Initialized:
                case DeviceState::Stopped:
                case DeviceState::Stopping:
                    return State::Stopped;
                case DeviceState::Capturing:
                case DeviceState::Starting:
                    return State::Playing;
                case DeviceState::Error:
                    return State::Error;
                default:
                    throw std::runtime_error("Unexpected DeviceState");
            }
        };

        void SetCallback(const CallBackT cb) override {
            callback_ = cb;
            capture_->SetCallback([this](auto audio) {
                this->checkNewAudio(audio);
                callback_(audio);
            });
        };

        void Play() override {
            if (capture_->GetDeviceState() == DeviceState::Initialized || capture_->GetDeviceState() == DeviceState::Stopped) {
                thread_ = std::thread(&WinAudioSource::process, this);
                if (const auto res = capture_->StartCapture()) {
                    throw std::runtime_error("StartCapture failed: " + hresult_to_string(res));
                }
                std::lock_guard lock(started_mutex_);
                started_ = true;
                started_condition_.notify_all();
            }
        };

        void Stop() override {
            if (capture_->GetDeviceState() == DeviceState::Capturing) {
                if (const auto res = capture_->StopCapture()) {
                    throw std::runtime_error("StopCapture failed: " + hresult_to_string(res));
                }
            }
            thread_.join();
        }

        uint32_t GetPid() override {
            return capture_->GetPid();
        }

        bool HasSignal() override {
            return active_;
        };
    };

    template class WinAudioSource<int16_t>;

}
