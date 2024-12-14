//
// Created by pavel on 11/21/2024.
//


#include <audioclientactivationparams.h>
#include "WinCapture.h"

#include <thread>
#include <stdexcept>

#include "util.hpp"


using SourceState =  recorder::IAudioSource<int16_t>::State ;
SourceState WinCapture::GetState() {
    switch (m_DeviceState) {
        case DeviceState::Uninitialized:
            return Uninitialized;
        case DeviceState::Initialized:
        case DeviceState::Stopped:
        case DeviceState::Stopping:
            return Stopped;
        case DeviceState::Capturing:
        case DeviceState::Starting:
            return Playing;
        case DeviceState::Error: // TODO Do something about that
            return Uninitialized;
        default:
            throw std::runtime_error("Unexpected DeviceState");
    }
}

void WinCapture::SetCallback(const CallBackT callback) {
    m_SampleReadyCallback = callback;
}

void WinCapture::Play() {
    if (m_DeviceState == DeviceState::Initialized || m_DeviceState == DeviceState::Stopped) {
        if (const auto res = StartCapture()) {
            throw std::runtime_error("StartCapture failed: " + hresult_to_string(res));
        }
    }
}

void WinCapture::Stop() {
    if (m_DeviceState == DeviceState::Capturing) {
        if (const auto res = StopCapture()) {
            throw std::runtime_error("StopCapute failed: " + hresult_to_string(res));
        }
    }
}

uint32_t WinCapture::GetPid() {
    return m_pid;
}

WinCapture::WinCapture(const recorder::AudioFormat format, const uint32_t bufferSizeNs,
                       const CallBackT &callback, const DWORD pid) {
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

HRESULT WinCapture::Initialize() {
    RETURN_IF_FAILED(m_ActivateCompleteEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_BufferReadyEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_CaptureStoppedEvent.create(wil::EventOptions::None));
    return S_OK;
}

WAVEFORMATEX* WinCapture::GetFormat() {
    return &m_format;
}



HRESULT WinCapture::ActivateAudioInterface() {
    return SetDeviceErrorIfFailed([&]() -> HRESULT {
        AUDIOCLIENT_ACTIVATION_PARAMS params = {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        params.ProcessLoopbackParams.TargetProcessId = m_pid;

        PROPVARIANT activate_params = {};
        activate_params.vt = VT_BLOB;
        activate_params.blob.cbSize = sizeof(params);
        activate_params.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

        wil::com_ptr_nothrow<IActivateAudioInterfaceAsyncOperation> activateAudioInterfaceOp;
        RETURN_IF_FAILED(
            ActivateAudioInterfaceAsync(
                VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                __uuidof(IAudioClient),
                &activate_params,
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
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                m_bufferSizeNs, m_bufferSizeNs, &m_format, nullptr));

        RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));

        // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
        RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_BufferReadyEvent.get()));

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
    UINT32 num_frames = 0;
    BYTE* data = nullptr;
    DWORD flags;
    UINT64 dev_pos = 0;
    UINT64 qpc_pos = 0;

    // The audio engine is free to accumulate buffer in various ways:
    // a. as a single packet of 480 samples, OR
    // b. as a packet of 80 samples plus a packet of 400 samples, OR
    // c. as 48 packets of 10 samples each.

    // So every time this routine runs, we need to read ALL the packets
    // that are now available;

    while (SUCCEEDED(m_AudioCaptureClient->GetNextPacketSize(&num_frames)) && num_frames > 0)
    {
        RETURN_IF_FAILED(m_AudioCaptureClient->GetBuffer(&data, &num_frames, &flags, &dev_pos, &qpc_pos));

        // Write to callback
        if (m_DeviceState != DeviceState::Stopping)
        {
            m_SampleReadyCallback(std::span<int16_t>(reinterpret_cast<int16_t*>(data), num_frames * m_format.nChannels));
        }

        RETURN_IF_FAILED(m_AudioCaptureClient->ReleaseBuffer(num_frames));
    }

    return S_OK;
}

HRESULT WinCapture::StartCapture() {
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
        if (!m_BufferReadyEvent.wait()) break;
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