//
// Created by pavel on 11/21/2024.
//

#ifndef WINCAPTURE_H
#define WINCAPTURE_H

#include <atomic>
#include <Audioclient.h>
#include <functional>
#include <mmdeviceapi.h>
#include <guiddef.h>
#include <mfapi.h>

#include <wrl/implements.h>
#include <wil/com.h>
#include <wil/result.h>

using namespace Microsoft::WRL;

class WinCapture : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
            IActivateAudioInterfaceCompletionHandler> {
public:
    WinCapture(uint16_t channels, uint32_t rate, uint16_t bitsPerSample);
    ~WinCapture() override = default;


// protected:
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
    wil::com_ptr_nothrow<IAudioClient> m_AudioClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_AudioCaptureClient;

    wil::unique_event_nothrow m_ActivateCompleteEvent;
    wil::unique_event_nothrow m_SampleReadyEvent;
    wil::unique_event_nothrow m_CaptureStoppedEvent;


    std::function<void(const void*, const uint32_t)> m_SampleReadyCallback;

    HRESULT Initialize();

    HRESULT StartCapture(DWORD processId);
    HRESULT StopCapture();
    HRESULT CaptureLoop() const;

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *activateOperation) override;

    HRESULT OnAudioSampleRequested() const;

    HRESULT ActivateAudioInterface(DWORD pid);
    HRESULT SetDeviceErrorIfFailed(HRESULT errorCode);

    HRESULT InitializeLoopbackCapture();

};

#endif //WINCAPTURE_H
