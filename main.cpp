#include <cassert>
#include <iostream>
#include <string>

#include <semaphore>
#include <thread>

#include <mfapi.h>

#include "WinCapture.h"

#define FORMAT sizeof(uint16_t)
#define SAMPLE_RATE 16000
#define CHANNELS 1

FILE* file;
uint32_t total_write = SAMPLE_RATE * 5;

std::binary_semaphore write_complete{0};

void data_callback(const void* data, const uint32_t frameCount)
{
    const auto audio_in = static_cast<const int16_t*>(data);
    std::cout << ".";
    for (int i = 0; i < frameCount; i++) {
    }

    if (total_write > 0) {
        std::cout << frameCount << std::endl;;
        const auto to_write = frameCount < total_write ? frameCount : total_write;
        const auto written = std::fwrite(audio_in, sizeof(int16_t), to_write, file);
        assert(written == to_write);
        total_write -= written;
    } else if (total_write == 0) {
        write_complete.release();
        total_write = 0;
    }
}

int main(const int argc, char const *argv[]) {
    const uint32_t pid = std::stoi(argc > 1 ? argv[1] : "0");
    std::cout << "Process ID: " << pid << std::endl;

    file = std::fopen("test.wav", "wb");

    auto fc = FormatConfig {
        .channels = CHANNELS,
        .sampleRate = SAMPLE_RATE,
        .bitsPerSample = 16,
    };

    auto wc = WinCapture(fc, 200000, data_callback, pid);

    const auto format = wc.GetFormat();
    const auto data_size = total_write * static_cast<DWORD>(FORMAT) * CHANNELS;
    const DWORD header[] = {
        FCC('RIFF'),
        data_size + 44 - 8, // header size - first two fields
        FCC('WAVE'),
        FCC('fmt '),
        sizeof(*format),
    };
    std::fwrite(header, sizeof(header), 1, file);
    std::fwrite(format, sizeof(*format), 1, file);
    const DWORD data[] = {FCC('data'), data_size};
    std::fwrite(data, sizeof(data), 1, file);

    const auto hr = [&] {
        RETURN_IF_FAILED(wc.Initialize());
        RETURN_IF_FAILED(wc.ActivateAudioInterface());
        RETURN_IF_FAILED(wc.StartCapture());
        return S_OK;
    }();

    if (FAILED(hr))
    {
        wil::unique_hlocal_string message;
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, hr,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<PWSTR>(&message), 0, nullptr);
        std::wcout << L"Failed to start capture\n0x" << std::hex << hr << L": " << message.get() << L"\n";
        std::wcout.flush();
        return hr;
    } else {
        std::thread thread([&] {
            RETURN_IF_FAILED(wc.CaptureLoop());
            return S_OK;
        });

        write_complete.acquire();
        RETURN_IF_FAILED(wc.StopCapture());
        thread.join();
    }
    std::fclose(file);
    return 0;
}
