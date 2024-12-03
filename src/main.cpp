#include <cassert>
#include <iostream>
#include <string>

#include <semaphore>
#include <thread>

#include <mfapi.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>

#include "hwid.hpp"
#include "WinCapture.h"

#define FORMAT sizeof(uint16_t)
#define SAMPLE_RATE 16000
#define CHANNELS 1

FILE* file;
uint32_t total_write = SAMPLE_RATE * 0.1;

std::binary_semaphore write_complete{0};

void data_callback(const void* data, const uint32_t frameCount)
{
    const auto audio_in = static_cast<const int16_t*>(data);
    static std::string output;
    output.resize(frameCount/2 + 1);
    assert(frameCount % 2 == 0);
    for (int i = 0; i < frameCount; i += 2) {
        const auto val = audio_in[i] + audio_in[i + 1];
        output[i / 2] = [&] {
            switch (abs(val)) {
                case 0:
                    return '_';
                case 1:
                    return '.';
                case 2:
                    return ':';
                default:
                    return '|';
            }
        }();
    }

    SPDLOG_TRACE("{}", output);

    if (total_write > 0) {
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
    const auto logger = spdlog::stdout_color_mt("main");
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s::%!] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    set_default_logger(logger);

    auto uuid = get_uuid();
    SPDLOG_INFO("Machine UUID = {}", uuid);

    const uint32_t pid = std::stoi(argc > 1 ? argv[1] : "0");
    SPDLOG_INFO("Pid = {}", pid);

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
        RETURN_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
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
