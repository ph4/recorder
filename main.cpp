#include <iostream>
#include <string>
#include <algorithm>
#include <functional>

#define MINIAUDIO_IMPLEMENTATION
#define MA_FORCE_UWP
#include "miniaudio.h"
#include <semaphore>
#include <thread>

#include "WinCapture.h"

#define FORMAT ma_format_s16
#define SAMPLE_RATE 16000
#define CHANNELS 1

static ma_encoder encoder;
static uint32_t total_write = SAMPLE_RATE * 5;

std::binary_semaphore write_complete{0};

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, const ma_uint32 frameCount)
{
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid, and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    const auto audio_in = static_cast<const ma_int16*>(pInput);
    const auto encoder = static_cast<ma_encoder*>(pDevice->pUserData);
    std::cout << ".";
    for (int i = 0; i < frameCount; i++) {
    }

    ma_uint64 written;
    if (total_write > 0) {
        std::cout << frameCount << std::endl;;
        const auto to_write = frameCount < total_write ? frameCount : total_write;
        assert(ma_encoder_write_pcm_frames(encoder, audio_in, to_write, &written) == MA_SUCCESS);
        assert(written == to_write);
        total_write -= written;
    } else if (total_write == 0) {
        write_complete.release();
        total_write = 0;
    }
}

void my_callback(const void* data, const uint32_t countFrames) {
    ma_device device;
    device.pUserData = &encoder;
    data_callback(&device, nullptr, data, countFrames);
}

int main(int argc, char const *argv[]) {
    uint32_t pid = std::stoi(argc > 1 ? argv[1] : "0");
    std::cout << "Process ID: " << pid << std::endl;

    ma_encoder_config enc_config = ma_encoder_config_init(ma_encoding_format_wav, FORMAT, CHANNELS, SAMPLE_RATE);
    ma_encoder_init_file("test.wav", &enc_config, &encoder);

    if (FALSE) {
        ma_device_config config = {};
        config.deviceType = ma_device_type_loopback;
        config.playback = {.format = FORMAT, .channels = CHANNELS};
        config.capture = {.format = FORMAT, .channels = CHANNELS};
        config.sampleRate = SAMPLE_RATE;
        config.dataCallback = data_callback;
        config.wasapi = { .loopbackProcessID = pid};
        config.pUserData = &encoder;
        config.noFixedSizedCallback = true;
        config.wasapi.loopbackProcessExclude = false;

        ma_context context;
        ma_context_init(nullptr, 0, nullptr, &context);
        ma_device device;
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            return -1;
        }
        ma_device_start(&device);

        write_complete.acquire();
        ma_encoder_uninit(&encoder);
        ma_device_uninit(&device);
    } else {
        WinCapture wc(CHANNELS, SAMPLE_RATE, FORMAT * 8);
        wc.m_SampleReadyCallback = my_callback;
        auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (!hr)
            hr = wc.StartCapture(pid);
        if (FAILED(hr))
        {
            wil::unique_hlocal_string message;
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, hr,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<PWSTR>(&message), 0, nullptr);
            std::wcout << L"Failed to start capture\n0x" << std::hex << hr << L": " << message.get() << L"\n";
            std::wcout.flush();
            return hr;
        }
        std::thread thread([&] { RETURN_IF_FAILED(wc.CaptureLoop()); });

        write_complete.acquire();
        RETURN_IF_FAILED(wc.StopCapture());
        thread.join();
        ma_encoder_uninit(&encoder);
    }
    return 0;
}
