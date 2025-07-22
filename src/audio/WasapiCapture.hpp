//
// Created by pavel on 11/21/2024.
//

#ifndef WINCAPTURE_H
#define WINCAPTURE_H

#include <array>
#include <functional>
#include <span>

#include <Audioclient.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <wrl/implements.h>

#include <audioclientactivationparams.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <spdlog/spdlog.h>

#include "AudioSource.hpp"
#include "util.hpp"

using namespace Microsoft::WRL;
// using namespace recorder::audio;

namespace recorder::audio::windows {
struct CompletionHandler
    : public RuntimeClass<
            RuntimeClassFlags<ClassicCom>,
            FtmBase,
            IActivateAudioInterfaceCompletionHandler> {
    wil::unique_event event_finished_;
    HRESULT activate_hr_ = E_FAIL;
    wil::com_ptr<IAudioClient> client_;

    CompletionHandler() { event_finished_.create(); }

    STDMETHOD(ActivateCompleted)(
          IActivateAudioInterfaceAsyncOperation *activateOperation
    ) override {
        auto set_finished = event_finished_.SetEvent_scope_exit();
        RETURN_IF_FAILED(
              activateOperation->GetActivateResult(&activate_hr_, client_.put_unknown())
        );
        if (FAILED(activate_hr_)) {
            SPDLOG_ERROR("ActivateAudioInterfaceAsync failed: {}", hresult_to_string(activate_hr_));
        }
        return S_OK;
    }
};

namespace CaptureEvents {
    enum CaptureEvents {
        PacketReady,
        Shutdown,
        Count,
    };
};
template <typename S> class WasapiCapture {
public:
    enum class DeviceState {
        Uninitialized,
        Error,
        Initialized,
        Starting,
        Capturing,
        Stopping,
        Stopped
    };
    using CallBackT = std::function<void(std::span<S>)>;

private:
    std::array<wil::unique_event, CaptureEvents::Count> events_;

    wil::unique_couninitialize_call couninit{wil::CoInitializeEx()};

    // CallBackT callback_;
    IAudioSink<S> *listener_;
    const uint32_t pid_;
    const bool is_loopback_;
    const WAVEFORMATEX format_{};
    const uint32_t buffer_size_ns_;

    std::optional<std::exception> error_;

    wil::com_ptr<IAudioClient> client_;
    wil::com_ptr<IAudioCaptureClient> capture_client_;

    std::thread capture_thread_;

public:
    WasapiCapture(const WasapiCapture &) = delete;

    WasapiCapture &operator=(const WasapiCapture &) = delete;

    std::optional<std::exception> GetError() const { return error_; }

    const WAVEFORMATEX *const GetFormat() const { return &format_; }

private:
    wil::unique_cotaskmem_string GetDeviceId() const {
        if (pid_ != 0) return wil::make_cotaskmem_string(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK);

        wil::unique_cotaskmem_string device_id;
        if (is_loopback_)
            THROW_IF_FAILED(StringFromIID(DEVINTERFACE_AUDIO_RENDER, &device_id));
        else
            THROW_IF_FAILED(StringFromIID(DEVINTERFACE_AUDIO_CAPTURE, &device_id));
        return device_id;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS GetActivationParams() const {
        auto activation_type = pid_ == 0 ? AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT
                                         : AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        return {
              .ActivationType = activation_type,
              .ProcessLoopbackParams = {
                    .TargetProcessId = pid_,
                    .ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE,
              },
        };
    }

    PROPVARIANT GetPropVariantActivationParams(AUDIOCLIENT_ACTIVATION_PARAMS *params) const {
        return {
              .vt = VT_BLOB,
              .blob = {
                    .cbSize = sizeof(*params),
                    .pBlobData = reinterpret_cast<BYTE *>(params),
              },
        };
    }

    void ActivateAudioInterface() {
        wil::com_ptr<IActivateAudioInterfaceAsyncOperation> async_op;

        auto device_id = GetDeviceId();
        auto params = GetActivationParams();
        auto propvar = GetPropVariantActivationParams(&params);

        auto completion_handler = CompletionHandler();

        SPDLOG_DEBUG(
              "[{}] Activating audio interface (deviceInterfacePath={})",
              GetCurrentThreadId(),
              wstringToString(device_id.get())
        );
        THROW_IF_FAILED(ActivateAudioInterfaceAsync(
              device_id.get(), __uuidof(IAudioClient), &propvar, &completion_handler, &async_op
        ));

        completion_handler.event_finished_.wait();
        client_ = completion_handler.client_;

        auto flags = (is_loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0)
                     | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                     | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;

        SPDLOG_DEBUG("[{}] Initializing audio client", GetCurrentThreadId());
        THROW_IF_FAILED(client_->Initialize(
              AUDCLNT_SHAREMODE_SHARED, flags, buffer_size_ns_, buffer_size_ns_, &format_, nullptr
        ));

        THROW_IF_FAILED(
              client_->GetService(__uuidof(IAudioCaptureClient), capture_client_.put_void())
        );

        THROW_IF_FAILED(client_->SetEventHandle(events_[CaptureEvents::PacketReady].get()));
    };

    void OnPacketReady() const {
        UINT32 num_frames = 0;

        // Every time this routine runs, we need to read ALL the packets
        // that are now available;
        THROW_IF_FAILED(capture_client_->GetNextPacketSize(&num_frames));
        while (num_frames > 0) {
            BYTE *data = nullptr;
            DWORD flags;
            THROW_IF_FAILED(
                  capture_client_->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr)
            );

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                listener_->OnNewPacket(
                      std::span<S>(reinterpret_cast<S *>(data), num_frames * format_.nChannels)
                );
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                SPDLOG_WARN("[{}] discontinuity packet", GetCurrentThreadId());
            }

            if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
                SPDLOG_WARN("[{}] timestamp error packet", GetCurrentThreadId());
            }

            THROW_IF_FAILED(capture_client_->ReleaseBuffer(num_frames));
            THROW_IF_FAILED(capture_client_->GetNextPacketSize(&num_frames));
        }
    }

    void Capture() {
        SPDLOG_TRACE("[{}] ActivateAudioInterface...", GetCurrentThreadId());
        ActivateAudioInterface();

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        THROW_IF_FAILED(client_->Start());

        auto stopping = false;
        while (!stopping) {
            auto event_id = WaitForMultipleObjects(
                  CaptureEvents::Count, events_[0].addressof(), FALSE, INFINITE
            );
            if (event_id == WAIT_FAILED) {
                throw std::runtime_error("WaitForMultipleObjects failed");
            };
            switch (event_id) {
                case CaptureEvents::PacketReady:
                    OnPacketReady();
                    break;
                case CaptureEvents::Shutdown:
                    stopping = true;
                    break;
                default:
                    throw std::runtime_error("Unexpected event_id");
            }
        }
        SPDLOG_TRACE("[{}] stop...", GetCurrentThreadId());
        THROW_IF_FAILED(client_->Stop());
    }

    std::string desctibe_;

    std::string Describe() {
        if (desctibe_.empty()) {
            desctibe_ = std::format("WinCapture(pid={}, is_loopback={})", pid_, is_loopback_);
        }
        return desctibe_;
    }

    void CaptureSafe() {
        SPDLOG_DEBUG("{} tid = {}", Describe(), GetCurrentThreadId());
        try {
            Capture();
        } catch (wil::ResultException e) {
            SPDLOG_ERROR("Capture exception: {}", e.what());
            error_ = std::move(e);
        } catch (std::exception e) {
            SPDLOG_ERROR("Capture exception: {}", e.what());
            error_ = std::move(e);
        }
    }

public:
    WasapiCapture(
          const recorder::audio::AudioFormat format,
          const uint32_t buffer_size_ns,
          // const CallBackT &callback,
          IAudioSink<S> *listener,
          const DWORD pid,
          bool is_loopback
    )
        : pid_(pid),
          is_loopback_(is_loopback),
          listener_(listener),
          buffer_size_ns_(buffer_size_ns),
          format_({
                .wFormatTag = WAVE_FORMAT_PCM,
                .nChannels = format.channels,
                .nSamplesPerSec = format.sampleRate,
                .nAvgBytesPerSec = format.channels * format.sampleRate * 2,
                .nBlockAlign = static_cast<WORD>(format.channels * 2), // 2 bytes per sample
                .wBitsPerSample = 16,
          }) {
        for (auto &event : events_) {
            event.create(wil::EventOptions::None);
        }

        if (!is_loopback_ && pid_ != 0) {
            throw std::invalid_argument("pid is invalid");
        }

        capture_thread_ = std::thread(&WasapiCapture::CaptureSafe, this);
    }

    ~WasapiCapture() {
        events_[CaptureEvents::Shutdown].SetEvent();
        capture_thread_.join();
    }
};

} // namespace recorder::audio::windows
#endif // WINCAPTURE_H
