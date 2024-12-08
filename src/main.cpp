#include <cassert>
#include <iostream>
#include <string>

#include <semaphore>
#include <thread>

#include <mfapi.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>

#include "hwid.hpp"
#include "OggOpusWriter.hpp"
#include "WinCapture.h"
#include "ChunkedRingBuffer.hpp"

#define FORMAT sizeof(uint16_t)
#define SAMPLE_RATE 16000
#define CHANNELS 2

uint32_t total_write_ms = 5000;

std::binary_semaphore write_complete{0};


int main(const int argc, char const *argv[]) {
    const auto logger = spdlog::stdout_color_mt("main");
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s::%!] [%^%l%$] %v");
        logger->set_level(spdlog::level::trace);
        set_default_logger(logger);

        auto uuid = get_uuid();
        SPDLOG_INFO("Machine UUID = {}", uuid);

        const uint32_t pid = std::stoi(argc > 1 ? argv[1] : "0");
        SPDLOG_INFO("Pid = {}", pid);


        auto fc = FormatConfig{
            .channels = CHANNELS,
            .sampleRate = SAMPLE_RATE,
            .bitsPerSample = 16,
        };

    auto wstream = std::ofstream("test.ogg", std::ios::binary | std::ios::trunc | std::ios::out);
    auto writer = OggOpusWriter<std::ofstream, 20, SAMPLE_RATE>(std::move(wstream), 48);
    if (auto res = writer.Init()) {
        SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
        return -1;
    }

    auto data_callback = [&writer](const void *data, const uint32_t frameCount) {
        const auto audio_in = static_cast<const int16_t *>(data);
        static auto buffer = ChunkedBuffer<int16_t, writer.FRAME_SIZE * 2, 3>();
        buffer.PushData(std::span(audio_in, frameCount));
        if (total_write_ms > 0) {
            while (buffer.n_chunks()) {
                writer.Push(buffer.ReadChunk());
                total_write_ms -= 20;
            }
        } else if (total_write_ms == 0) {
            write_complete.release();
            total_write_ms = 0;
        }
    };

    auto wc = WinCapture(fc, 200000, data_callback, pid);


    const auto hr = [&] {
        RETURN_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        RETURN_IF_FAILED(wc.Initialize());
        RETURN_IF_FAILED(wc.ActivateAudioInterface());
        RETURN_IF_FAILED(wc.StartCapture());
        return S_OK;
    }();

    if (FAILED(hr)) {
        wil::unique_hlocal_string message;
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                       nullptr, hr,
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
        auto res = writer.Finalize();
        if (std::holds_alternative<int>(res)) {
            SPDLOG_ERROR("Failed to finalize writer: {}", std::get<int>(res));
            return -1;
        }
        else {
            auto w = std::move(std::get<0>(res));
            w.close();
        }
    }
    return 0;
}
