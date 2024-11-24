//
// Created by pavel on 11/21/2024.
//

#include <audioclientactivationparams.h>
#include "WinCapture.h"

#include <thread>

WinCapture::WinCapture(const uint16_t channels = 1, const uint32_t rate = 48000,
                       const uint16_t bitsPerSample = 16) {
    m_format = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = channels,
        .nSamplesPerSec = rate,
        .wBitsPerSample = bitsPerSample,
    };
    m_format.nBlockAlign = m_format.nChannels * m_format.wBitsPerSample / 8;
    m_format.nAvgBytesPerSec = m_format.nSamplesPerSec * m_format.nBlockAlign;
}


HRESULT WinCapture::Initialize() {
    RETURN_IF_FAILED(m_ActivateCompleteEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_SampleReadyEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_CaptureStoppedEvent.create(wil::EventOptions::None));
    return S_OK;
}



HRESULT WinCapture::ActivateAudioInterface(const DWORD pid) {
    return SetDeviceErrorIfFailed([&]() -> HRESULT {
        AUDIOCLIENT_ACTIVATION_PARAMS params = {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        params.ProcessLoopbackParams.TargetProcessId = pid;

        PROPVARIANT activateParams = {};
        activateParams.vt = VT_BLOB;
        activateParams.blob.cbSize = sizeof(params);
        activateParams.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

        wil::com_ptr_nothrow<IActivateAudioInterfaceAsyncOperation> activateAudioInterfaceOp;
        RETURN_IF_FAILED(
            ActivateAudioInterfaceAsync(
                VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                __uuidof(IAudioClient),
                &activateParams,
                this, // IActivateAudioInterfaceCompletionHandler
                &activateAudioInterfaceOp));

        // Wait for activation completion

        m_ActivateCompleteEvent.wait();

        HRESULT hr;
        wil::com_ptr_nothrow<IUnknown> audioInterface;
        RETURN_IF_FAILED(activateAudioInterfaceOp->GetActivateResult(&hr, &audioInterface));
        RETURN_IF_FAILED(hr);
        RETURN_IF_FAILED(audioInterface.copy_to(&m_AudioClient));

        RETURN_IF_FAILED(
            m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                200000, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, &m_format, nullptr));

        RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));

        // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
        RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_SampleReadyEvent.get()));

        //Everything is ready
        m_DeviceState = DeviceState::Initialized;

        return S_OK;
    }());

};


HRESULT WinCapture::ActivateCompleted(IActivateAudioInterfaceAsyncOperation *activateOperation) {
    m_ActivateCompleteEvent.SetEvent();
    return S_OK;
}

//
//  OnAudioSampleRequested()
//
//  Called when audio device fires m_SampleReadyEvent
//
HRESULT WinCapture::OnAudioSampleRequested() const {
    UINT32 FramesAvailable = 0;
    BYTE* Data = nullptr;
    DWORD dwCaptureFlags;
    UINT64 u64DevicePosition = 0;
    UINT64 u64QPCPosition = 0;

    // A word on why we have a loop here;
    // Suppose it has been 10 milliseconds or so since the last time
    // this routine was invoked, and that we're capturing 48000 samples per second.
    //
    // The audio engine can be reasonably expected to have accumulated about that much
    // audio data - that is, about 480 samples.
    //
    // However, the audio engine is free to accumulate this in various ways:
    // a. as a single packet of 480 samples, OR
    // b. as a packet of 80 samples plus a packet of 400 samples, OR
    // c. as 48 packets of 10 samples each.
    //
    // In particular, there is no guarantee that this routine will be
    // run once for each packet.
    //
    // So every time this routine runs, we need to read ALL the packets
    // that are now available;
    //
    // We do this by calling IAudioCaptureClient::GetNextPacketSize
    // over and over again until it indicates there are no more packets remaining.
    while (SUCCEEDED(m_AudioCaptureClient->GetNextPacketSize(&FramesAvailable)) && FramesAvailable > 0)
    {
        RETURN_IF_FAILED(m_AudioCaptureClient->GetBuffer(&Data, &FramesAvailable, &dwCaptureFlags, &u64DevicePosition, &u64QPCPosition));

        // Write to callback
        if (m_DeviceState != DeviceState::Stopping)
        {
            m_SampleReadyCallback(Data, FramesAvailable);
        }

        RETURN_IF_FAILED(m_AudioCaptureClient->ReleaseBuffer(FramesAvailable));
    }

    return S_OK;
}

HRESULT WinCapture::StartCapture(const DWORD processId) {
    RETURN_IF_FAILED(Initialize());
    RETURN_IF_FAILED(ActivateAudioInterface(processId));
    RETURN_IF_FAILED(m_AudioClient->Start());

    m_DeviceState = DeviceState::Capturing;
    return S_OK;
}

HRESULT WinCapture::StopCapture() {
    m_DeviceState = DeviceState::Stopping;
    m_CaptureStoppedEvent.wait();
    m_DeviceState = DeviceState::Stopped;
    return S_OK;
}

HRESULT WinCapture::CaptureLoop() const {
    while (m_DeviceState != DeviceState::Stopping) {
        if (!m_SampleReadyEvent.wait()) break;
        if FAILED(OnAudioSampleRequested()) break;

    }
    m_CaptureStoppedEvent.SetEvent();
    return S_OK;
}

HRESULT WinCapture::SetDeviceErrorIfFailed(const HRESULT errorCode)
{
    if (FAILED(errorCode))
    {
        m_DeviceState = DeviceState::Error;
    }
    return errorCode;
}
