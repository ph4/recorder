//
// Created by pavel on 24.03.2025.
//
#ifndef AUDIODEVICEMONITOR_H
#define AUDIODEVICEMONITOR_H

#include <span>
#include <atomic>
#include <thread>

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <spdlog/spdlog.h>

#include "AudioSource.hpp"

using namespace std::chrono;

namespace recorder::audio {

template <typename S>
class AudioDeviceMonitor : public IMMNotificationClient {
private:
    std::atomic<bool> has_active_device;
    IMMDeviceEnumerator* pEnumerator;
    AudioFormat format;
    typename AudioSource<S>::CallBackT callback;
    std::thread silent_thread;
    bool running;

public:
    AudioDeviceMonitor(AudioFormat format, typename AudioSource<S>::CallBackT cb)
        : has_active_device(false), pEnumerator(nullptr), format(format), callback(cb), running(true) {
        CoCreateInstance(__uuidof(MMDeviceEnumerator),
                         nullptr,
                         CLSCTX_ALL,
                         __uuidof(IMMDeviceEnumerator),
                         (void **) &pEnumerator);
        if (pEnumerator) {
            pEnumerator->RegisterEndpointNotificationCallback(this);
            UpdateActiveDeviceStatus();
        }

        // Start a thread to simulate silence when no devices exist
        silent_thread = std::thread([this]() { GenerateSilcence(); });
    }

    virtual ~AudioDeviceMonitor() {
        running = false;
        if (silent_thread.joinable()) silent_thread.join();

        if (pEnumerator) {
            pEnumerator->UnregisterEndpointNotificationCallback(this);
            pEnumerator->Release();
        }
    }

    bool IsActiveDevicePresent() const {
        return has_active_device.load();
    }

    // Checks if there are any active recording devices
    void UpdateActiveDeviceStatus() {
        IMMDeviceCollection* pCollection = nullptr;
        UINT count = 0;
        if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection))) {
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
                callback(span);
            }
            std::this_thread::sleep_for(cycle_size - (high_resolution_clock::now() - start_time));
        }
    }

    // IUnknown Methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *ppvObject = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // IMMNotificationClient Methods
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override {
        SPDLOG_DEBUG("OnDeviceStateChanged");
        UpdateActiveDeviceStatus();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDeviceAdded");
        UpdateActiveDeviceStatus();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDeviceRemoved");
        UpdateActiveDeviceStatus();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR deviceId) override {
        SPDLOG_DEBUG("OnDefaultDeviceChanged");
        if (flow == eCapture) {
            SPDLOG_DEBUG("flow == eCapture");
            UpdateActiveDeviceStatus();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override {
        return S_OK;
    }
};

}
#endif
