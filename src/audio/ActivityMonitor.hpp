#pragma once

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <windows.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <psapi.h>

#include "audio_core.hpp"

using namespace std::chrono;
using namespace std::chrono_literals;
namespace recorder::audio {

template <typename S, bool SignalAware> class IActivityMonitor {};

template <typename S> class ISignalActivityMonitor
    : public IActivityMonitor<S, true>
    , public IAudioSink<S> {};

template <typename S> class ActvityMonitorNull : public IActivityMonitor<S, false> {};

template <typename S> class ActivityMonitorSilence : public ISignalActivityMonitor<S> {
    size_t frames_silent_ = std::numeric_limits<size_t>::max();
    bool active_ = false;
    int max_silence_seconds_;
    const audio::AudioFormat format_;
    IActivityListener<S> *const listener_ = nullptr;
    const size_t samples_per_sec = format_.sampleRate * format_.channels;

public:
    ActivityMonitorSilence(
          IActivityListener<S> *listener, int max_silence_seconds, audio::AudioFormat format
    )
        : listener_(listener), max_silence_seconds_(max_silence_seconds), format_(format) {}
    void OnNewPacket(std::span<S> packet) override {
        // Windows sometimes has samples with value of +-1 on silent streams
        auto has_signal =
              std::any_of(packet.begin(), packet.end(), [&](auto s) { return std::abs(s) > 1; });
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
    };
};

template <typename S> class ActivityMonitorWhatsapp : public ISignalActivityMonitor<S> {
    wil::com_ptr<IAudioMeterInformation> meter_;
    IActivityListener<S> *listener_;
    AudioFormat format_;
    bool active_ = false;
    int max_silence_seconds_ = 5;
    double silent_for_seconds_ = 0;
    steady_clock::time_point last_refresh_;

    std::wstring GetProcessName(DWORD pid) {
        WCHAR name[MAX_PATH] = {};
        wil::unique_handle hProcess{
              OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)
        };
        if (hProcess) {
            GetModuleBaseNameW(hProcess.get(), nullptr, name, MAX_PATH);
        }
        return name;
    }

    wil::com_ptr<IAudioMeterInformation> GetAudioMeterByAppName(const std::wstring &appName) {
        wil::com_ptr<IMMDeviceEnumerator> enumerator;
        wil::com_ptr<IMMDevice> device;
        wil::com_ptr<IAudioSessionManager2> sessionManager;
        wil::com_ptr<IAudioSessionEnumerator> sessionEnumerator;

        THROW_IF_FAILED(CoCreateInstance(
              __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)
        ));

        THROW_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device));

        THROW_IF_FAILED(device->Activate(
              __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()
        ));

        THROW_IF_FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator));

        int sessionCount = 0;
        sessionEnumerator->GetCount(&sessionCount);

        for (int i = 0; i < sessionCount; ++i) {
            wil::com_ptr<IAudioSessionControl> sessionControl;
            if (FAILED(sessionEnumerator->GetSession(i, &sessionControl))) continue;

            wil::com_ptr<IAudioSessionControl2> sessionControl2;
            if (FAILED(sessionControl->QueryInterface(&sessionControl2))) continue;

            DWORD pid = 0;
            sessionControl2->GetProcessId(&pid);
            if (_wcsicmp(GetProcessName(pid).c_str(), appName.c_str()) == 0) {
                wil::com_ptr<IAudioMeterInformation> meter;
                if (SUCCEEDED(sessionControl2->QueryInterface(&meter))) {
                    CoUninitialize();
                    return meter;
                }
            }
        }

        return nullptr;
    }

    void Refresh() {
        auto now = steady_clock::now();
        if (now > last_refresh_ + 10min) {
            meter_ = GetAudioMeterByAppName(L"WhatsApp.exe");
            last_refresh_ = now;
        }
    }

public:
    ActivityMonitorWhatsapp(
          IActivityListener<S> *listener, AudioFormat format, int max_silence_seconds
    )
        : listener_(listener), format_(format), max_silence_seconds_(max_silence_seconds) {
        meter_ = GetAudioMeterByAppName(L"WhatsApp.exe");
        if (meter_ == nullptr) {
            throw std::runtime_error("WhatsApp not found");
        }
    }

    void OnNewPacket(std::span<S> packet) override {
        float peak = 0;
        float threshold = 0.01;

        Refresh(); // Periodically refresh meter_ to avoid it becoming invalid

        auto hr = meter_->GetPeakValue(&peak);
        if (FAILED(hr)) [[unlikely]] {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                SPDLOG_WARN("AudioMeterInformation device invalidated");
                meter_ = GetAudioMeterByAppName(L"WhatsApp.exe");
            }
            hr = meter_->GetPeakValue(&peak);
            if (FAILED(hr)) {
                throw std::runtime_error("GetPeakValue failed");
            }
        }
        if (!active_ && peak > threshold) {
            active_ = true;
            silent_for_seconds_ = 0;
            listener_->OnActive();
        } else if (active_ && peak > threshold) {
            silent_for_seconds_ = 0;
        } else if (active_ && peak < threshold) {
            silent_for_seconds_ += (double)packet.size() / (format_.sampleRate * format_.channels);
            if (silent_for_seconds_ > max_silence_seconds_) {
                active_ = false;
                listener_->OnInactive();
            }
        }
    };
};

} // namespace recorder::audio
