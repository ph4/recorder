//
// Created by pavel on 24.03.2025.
//
#ifndef AUDIODEVICEMONITOR_H
#define AUDIODEVICEMONITOR_H

#include <atomic>
#include <span>
#include <thread>

#include <windows.h>

#include <functiondiscoverykeys.h>
#include <mmdeviceapi.h>

#include <spdlog/spdlog.h>
#include <wil/com.h>
#include <wrl/implements.h>

#include "AudioSource.hpp"

using namespace std::chrono;
using namespace Microsoft::WRL;

namespace recorder::audio {

struct DeviceChangeListener
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IMMNotificationClient> {
    std::function<void()> callback;

    DeviceChangeListener(std::function<void()> callback) : callback(callback) {}

    // IMMNotificationClient Methods
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override {
        SPDLOG_DEBUG("OnDeviceStateChanged");
        callback();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDeviceAdded");
        callback();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDeviceRemoved");
        callback();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDefaultDeviceChanged");
        callback();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override {
        return S_OK;
    }
};

template <typename S> class InactiveAudioDeviceHandler {
public:
    using CallBackT = std::function<void(std::span<S>)>;

private:
    DeviceChangeListener notification_callback;
    std::atomic<bool> has_active_device;
    wil::com_ptr<IMMDeviceEnumerator> enumerator;
    AudioFormat format;
    IAudioSink<S>* sink;
    std::thread silent_thread;
    bool running;

public:
    InactiveAudioDeviceHandler(AudioFormat format, IAudioSink<S>* sink)
        : notification_callback([this]() { UpdateActiveDeviceStatus(); }),
          has_active_device(false),
          format(format),
          sink(sink),
          running(true) {
        CoCreateInstance(
              __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put())
        );
        if (enumerator) {
            enumerator->RegisterEndpointNotificationCallback(&notification_callback);
            UpdateActiveDeviceStatus();
        }

        // Start a thread to simulate silence when no devices exist
        silent_thread = std::thread([this]() { GenerateSilcence(); });
    }

    virtual ~InactiveAudioDeviceHandler() {
        running = false;
        if (silent_thread.joinable()) silent_thread.join();

        if (enumerator) {
            enumerator->UnregisterEndpointNotificationCallback(&notification_callback);
        }
    }

private:
    bool IsActiveDevicePresent() const { return has_active_device.load(); }

    // Checks if there are any active recording devices
    void UpdateActiveDeviceStatus() {
        IMMDeviceCollection* pCollection = nullptr;
        UINT count = 0;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection)
            )) {
            pCollection->GetCount(&count);
            pCollection->Release();
        }
        SPDLOG_DEBUG("Active capture device count = {}", count);
        has_active_device.store(count > 0);
    }

    // Simulate silence by calling callback manually
    void GenerateSilcence() {
        auto cycle_size = 20ms;
        auto samples_per_cycle = format.sampleRate * cycle_size.count() / 1000 * format.channels;

        auto buffer = std::vector(samples_per_cycle, static_cast<S>(0));
        std::span span(buffer.data(), buffer.size());

        while (running) {
            auto start_time = high_resolution_clock::now();
            if (!IsActiveDevicePresent()) {
                SPDLOG_TRACE("Calling callback with silence");
                sink->OnNewPacket(span);
            }
            std::this_thread::sleep_for(cycle_size - (high_resolution_clock::now() - start_time));
        }
    }
};

} // namespace recorder::audio
#endif
