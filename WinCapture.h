//
// Created by pavel on 11/21/2024.
//

#ifndef WINCAPTURE_H
#define WINCAPTURE_H

#include <atomic>
#include <Audioclient.h>
#include <functional>
#include <mmdeviceapi.h>

#include <wrl/implements.h>
#include <wil/com.h>
#include <wil/result.h>

using namespace Microsoft::WRL;

struct FormatConfig {
    uint16_t channels;
    uint32_t sampleRate;
    uint16_t bitsPerSample;
};

class WinCapture : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
            IActivateAudioInterfaceCompletionHandler> {
    using CallBackT = void(*)(const void *, uint32_t);
public:
    WinCapture(FormatConfig format, uint32_t bufferSizeNs, const CallBackT &callback, DWORD pid);

    HRESULT Initialize();
    WAVEFORMATEX* GetFormat();
    HRESULT ActivateAudioInterface();

    HRESULT StartCapture();
    HRESULT StopCapture();
    HRESULT CaptureLoop() const;

protected:
    enum class DeviceState {
        Uninitialized,
        Error,
        Initialized,
        Starting,
        Capturing,
        Stopping,
        Stopped
    };

    std::atomic<DeviceState> m_DeviceState { DeviceState::Uninitialized };
    WAVEFORMATEX m_format{};
    uint32_t m_bufferSizeNs;
    uint32_t m_pid;
    wil::com_ptr_nothrow<IAudioClient> m_AudioClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_AudioCaptureClient;

    wil::unique_event_nothrow m_ActivateCompleteEvent;
    wil::unique_event_nothrow m_BufferReadyEvent;
    wil::unique_event_nothrow m_CaptureStoppedEvent;


    std::function<void(const void*, const uint32_t)> m_SampleReadyCallback;

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *activateOperation) override;

    HRESULT OnAudioSampleRequested() const;

    HRESULT SetDeviceErrorIfFailed(HRESULT errorCode);

};

#endif //WINCAPTURE_H
