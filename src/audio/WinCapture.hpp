//
// Created by pavel on 11/21/2024.
//

#ifndef WINCAPTURE_H
#define WINCAPTURE_H

#include <span>
#include <functional>

#include <Audioclient.h>

#include <wrl/implements.h>
#include <wil/com.h>
#include <wil/result.h>

#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>

#include "util.hpp"
#include "AudioSource.hpp"

DEFINE_GUID(inline DEVINTERFACE_AUDIO_CAPTURE_, 0x2eef81be, 0x33fa, 0x4800, 0x96, 0x70, 0x1c, 0xd4, 0x74, 0x97, 0x2c,
            0x3f);
DEFINE_GUID(inline DEVINTERFACE_AUDIO_RENDER_, 0xe6327cad, 0xdcec, 0x4949, 0xae, 0x8a, 0x99, 0x1e, 0x97, 0x6a, 0x79,
            0xd2);



using namespace Microsoft::WRL;

template<typename S>
class WinCapture : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
            IActivateAudioInterfaceCompletionHandler> {
public:
    WinCapture(const WinCapture &) = delete;

    WinCapture &operator=(const WinCapture &) = delete;

    WinCapture(WinCapture &&) = default;

    WinCapture &operator=(WinCapture &&) = default;

    ~WinCapture() override;

    using CallBackT = std::function<void(std::span<S>)>;

    WinCapture(recorder::audio::AudioFormat format,
               uint32_t bufferSizeNs,
               const CallBackT &callback,
               DWORD pid,
               bool loopback);

    HRESULT Initialize();

    WAVEFORMATEX *GetFormat();

    HRESULT ActivateAudioInterface();

    HRESULT StartCapture();

    HRESULT StopCapture();

    HRESULT CaptureLoop() const;

    enum class DeviceState {
        Uninitialized,
        Error,
        Initialized,
        Starting,
        Capturing,
        Stopping,
        Stopped
    };

    DeviceState GetDeviceState() const;

    uint32_t GetPid() const;

    void SetCallback(const CallBackT &callback);

protected:
    DeviceState m_DeviceState{DeviceState::Uninitialized};
    WAVEFORMATEX m_format{};
    uint32_t m_bufferSizeNs;
    uint32_t m_pid;
    wil::com_ptr_nothrow<IAudioClient> m_AudioClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_AudioCaptureClient;

    wil::unique_event_nothrow m_ActivateCompleteEvent;
    wil::unique_event_nothrow m_BufferReadyEvent;
    wil::unique_event_nothrow m_CaptureStoppedEvent;


    CallBackT m_SampleReadyCallback;
    bool m_loopback;

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *activateOperation) override;

    HRESULT OnAudioSampleRequested() const;

    HRESULT SetDeviceErrorIfFailed(HRESULT errorCode);
};


template<typename S>
void WinCapture<S>::SetCallback(const CallBackT &callback) {
    m_SampleReadyCallback = callback;
}

template<typename S>
typename WinCapture<S>::DeviceState WinCapture<S>::GetDeviceState() const {
    return m_DeviceState;
}

template<typename S>
uint32_t WinCapture<S>::GetPid() const {
    return m_pid;
}

template<typename S>
WinCapture<S>::WinCapture(const recorder::audio::AudioFormat format, const uint32_t bufferSizeNs,
                          const CallBackT &callback, const DWORD pid, bool loopback) {
    m_loopback = loopback;
    m_SampleReadyCallback = callback;
    m_format = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = format.channels,
        .nSamplesPerSec = format.sampleRate,
        .wBitsPerSample = 16,
    };
    m_format.nBlockAlign = m_format.nChannels * m_format.wBitsPerSample / 8;
    m_format.nAvgBytesPerSec = m_format.nSamplesPerSec * m_format.nBlockAlign;
    m_bufferSizeNs = bufferSizeNs;

    m_pid = pid;
}


template<typename S>
WinCapture<S>::~WinCapture() {
    switch (m_DeviceState) {
        case DeviceState::Stopping:
            m_CaptureStoppedEvent.wait();
        break;
        case DeviceState::Capturing:
            StopCapture();
        break;
    }
}

template<typename S>
HRESULT WinCapture<S>::Initialize() {
    RETURN_IF_FAILED(m_ActivateCompleteEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_BufferReadyEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_CaptureStoppedEvent.create(wil::EventOptions::None));
    return S_OK;
}

template<typename S>
WAVEFORMATEX *WinCapture<S>::GetFormat() {
    return &m_format;
}


template<typename S>
HRESULT WinCapture<S>::ActivateAudioInterface() {
    return SetDeviceErrorIfFailed([&]() -> HRESULT {
        wil::com_ptr_nothrow<IActivateAudioInterfaceAsyncOperation> activateAudioInterfaceOp;

        AUDIOCLIENT_ACTIVATION_PARAMS params = {};
        params.ActivationType = m_pid == 0
                                    ? AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT
                                    : AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        params.ProcessLoopbackParams.TargetProcessId = m_pid;

        PROPVARIANT activate_params = {};
        activate_params.vt = VT_BLOB;
        activate_params.blob.cbSize = sizeof(params);
        activate_params.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

        LPOLESTR id;
        if (m_pid == 0) {
            if (m_loopback) RETURN_IF_FAILED(StringFromIID(DEVINTERFACE_AUDIO_RENDER_, &id));
            else RETURN_IF_FAILED(StringFromIID(DEVINTERFACE_AUDIO_CAPTURE_, &id));
        }

        THROW_IF_FAILED(
            ActivateAudioInterfaceAsync(
                m_pid == 0 ? id : VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                __uuidof(IAudioClient),
                &activate_params,
                this, // IActivateAudioInterfaceCompletionHandler
                &activateAudioInterfaceOp));

        if (m_pid == 0) CoTaskMemFree(id);
        // Wait for activation completion

        m_ActivateCompleteEvent.wait();

        HRESULT hr;
        wil::com_ptr_nothrow<IUnknown> audioInterface;
        RETURN_IF_FAILED(activateAudioInterfaceOp->GetActivateResult(&hr, &audioInterface));
        RETURN_IF_FAILED(hr);
        RETURN_IF_FAILED(audioInterface.copy_to(&m_AudioClient));

        THROW_IF_FAILED(
            m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                (m_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0) | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                m_bufferSizeNs, m_bufferSizeNs, &m_format, nullptr));

        RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));

        // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
        RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_BufferReadyEvent.get()));

        //Everything is ready
        m_DeviceState = DeviceState::Initialized;

        return S_OK;
    }());
};


template<typename S>
HRESULT WinCapture<S>::ActivateCompleted(IActivateAudioInterfaceAsyncOperation *activateOperation) {
    m_ActivateCompleteEvent.SetEvent();
    return S_OK;
}

//
//  OnAudioSampleRequested()
//
//  Called when audio device fires m_SampleReadyEvent
//

template<typename S>
HRESULT WinCapture<S>::OnAudioSampleRequested() const {
    UINT32 num_frames = 0;
    BYTE *data = nullptr;
    DWORD flags;
    UINT64 dev_pos = 0;
    UINT64 qpc_pos = 0;

    // The audio engine is free to accumulate buffer in various ways:
    // a. as a single packet of 480 samples, OR
    // b. as a packet of 80 samples plus a packet of 400 samples, OR
    // c. as 48 packets of 10 samples each.

    // So every time this routine runs, we need to read ALL the packets
    // that are now available;

    while (SUCCEEDED(m_AudioCaptureClient->GetNextPacketSize(&num_frames)) && num_frames > 0) {
        RETURN_IF_FAILED(m_AudioCaptureClient->GetBuffer(&data, &num_frames, &flags, &dev_pos, &qpc_pos));

        // Write to callback
        if (m_DeviceState != DeviceState::Stopping) {
            m_SampleReadyCallback(
                std::span<int16_t>(reinterpret_cast<int16_t *>(data), num_frames * m_format.nChannels));
        }

        RETURN_IF_FAILED(m_AudioCaptureClient->ReleaseBuffer(num_frames));
    }

    return S_OK;
}

template<typename S>
HRESULT WinCapture<S>::StartCapture() {
    RETURN_IF_FAILED(m_AudioClient->Start());
    m_DeviceState = DeviceState::Capturing;

    return S_OK;
}

template<typename S>
HRESULT WinCapture<S>::StopCapture() {
    m_DeviceState = DeviceState::Stopping;
    m_CaptureStoppedEvent.wait();
    m_AudioClient->Stop();
    m_AudioClient->Reset();
    m_DeviceState = DeviceState::Stopped;
    return S_OK;
}

template<typename S>
HRESULT WinCapture<S>::CaptureLoop() const {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (m_DeviceState != DeviceState::Stopping) {
        if (!m_BufferReadyEvent.wait()) break;
        if FAILED(OnAudioSampleRequested()) break;
    }
    m_CaptureStoppedEvent.SetEvent();
    return S_OK;
}

template<typename S>
HRESULT WinCapture<S>::SetDeviceErrorIfFailed(const HRESULT errorCode) {
    if (FAILED(errorCode)) {
        m_DeviceState = DeviceState::Error;
    }
    return errorCode;
}

#endif //WINCAPTURE_H
