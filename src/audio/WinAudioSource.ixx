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

namespace recorder::audio::windows {
    using namespace recorder::audio;


    template<typename S>
    class SilenceActivityStatus final : public AudioActivityStatus<S> {
        bool active = false;
    public:
        SilenceActivityStatus(const std::function<void(AudioSourceActivation)> &onActivated,
                              const std::function<void(AudioSourceDeactivation)> &onDeactivated)
            : AudioActivityStatus<S>(onActivated, onDeactivated) { }
        bool isActive() override {
            return active;
        };

        bool checkNewAudio(std::span<S> audio) override {
            // Windows sometimes has samples with the value of 1 on silent streams
            auto has_audio = std::none_of(audio.begin(), audio.end(), [&](auto s) { return s > 1; });
            if (!active && has_audio ) {
                active = true;
                AudioSourceActivation data{
                    .timestamp = std::chrono::utc_clock::now(),
                    .activationSource = std::string("silence"),
                    .metadata = std::nullopt,
                };
                this->onActivated(data);
            } else if (active && !has_audio) {
                active = false;
                this->onDeactivated(AudioSourceDeactivation{});
            }
            return active;
        };
    };


    template<typename S>
    class WinAudioSource : public ProcessAudioSource<S> {
        bool started_ = false;
        std::mutex started_mutex_{};
        std::condition_variable started_condition_{};

        wil::com_ptr<WinCapture<S>> capture_;
        std::thread thread_;
        void process() {
            {
                std::unique_lock lock(started_mutex_);
                started_condition_.wait(lock, [this] { return started_; });
            }
            capture_->CaptureLoop();
        }
        using ProcessAudioSource<S>::ProcessAudioSource;
    public:
        explicit WinAudioSource(wil::com_ptr<WinCapture<S>> capture,
                                std::unique_ptr<AudioActivityStatus<S> > activationChecker)
            : ProcessAudioSource<S>(std::move(activationChecker)),
              capture_(std::move(capture)),
              thread_(std::thread(&WinAudioSource::process, this)) {
            if (capture_->GetDeviceState() != WinCapture<S>::DeviceState::Initialized) {
                throw std::runtime_error(
                    "WinAudioSource: Device should be in initialized state (not started or stopped)");
            }
            this->activationChecker = std::move(activationChecker);
        }

        using State = typename ProcessAudioSource<S>::State;
        using DeviceState = typename WinCapture<S>::DeviceState;
        using CallBackT = typename ProcessAudioSource<S>::CallBackT;

        State GetState() override {
            switch (capture_->GetDeviceState()) {
                case DeviceState::Uninitialized:
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
            capture_->SetCallback(cb);
        };

        void Play() override {
            if (capture_->GetDeviceState() == DeviceState::Initialized || capture_->GetDeviceState() == DeviceState::Stopped) {
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

        ~WinAudioSource() override
        {
            // capture_.m_AudioCaptureClient.reset();
            // capture_.m_AudioClient.reset();
            // capture_.Release();
        }

    };

    template class WinAudioSource<int16_t>;

    export template<typename S>
    std::unique_ptr<WinAudioSource<S>> get_source_for_pid(AudioFormat format, typename WinCapture<S>::CallBackT callback, uint32_t pid) {
        if (pid == 0) {
            throw std::invalid_argument("pid is invalid");
        }

        ComPtr<WinCapture<S>> wcp = Microsoft::WRL::Make<WinCapture<S>>(format, 200000, callback, pid);

        wil::com_ptr wc(wcp.Detach());

        THROW_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        THROW_IF_FAILED(wc->Initialize());
        THROW_IF_FAILED(wc->ActivateAudioInterface());

        auto ac = std::make_unique<SilenceActivityStatus<S>>([](AudioSourceActivation){}, [](AudioSourceDeactivation){});

        return std::make_unique<WinAudioSource<S>> (std::move(wc), std::move(ac));
    }

}
