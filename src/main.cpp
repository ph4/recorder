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
#include "audio/WinCapture.hpp"
#include "ChunkedRingBuffer.hpp"

import AudioSource;
import WinAudioSource;


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

    const uint32_t pid = argc > 1 ? std::stoi(argv[1]) : 0;
    total_write_ms = argc > 2 ? std::stoi(argv[2]) : 1000;
    SPDLOG_INFO("Pid = {}", pid);
    SPDLOG_INFO("total_write_ms = {}", total_write_ms);


    auto wstream = std::ofstream("test.ogg", std::ios::binary | std::ios::trunc | std::ios::out);
    auto writer = OggOpusWriter<std::ofstream, 20, SAMPLE_RATE, CHANNELS>(std::move(wstream), 48);
    if (auto res = writer.Init()) {
        SPDLOG_ERROR("Failed to initialize OggOpusWriter: {}", res);
        return -1;
    }

    auto data_callback = [&writer](std::span<int16_t> data) {
        if (total_write_ms > 0) {
            writer.Push(data);
            total_write_ms -= data.size() / CHANNELS * 1000 / SAMPLE_RATE;
        } else if (total_write_ms == 0) {
            write_complete.release();
            total_write_ms = 0;
        }
    };


    auto fc = recorder::audio::AudioFormat{
        .channels = CHANNELS,
        .sampleRate = SAMPLE_RATE,
    };

    auto source = recorder::audio::windows::get_source_for_pid<int16_t>(fc, data_callback, pid);
    source->Play();
    write_complete.acquire();
    source->Stop();

    auto res = writer.Finalize();
    if (std::holds_alternative<int>(res)) {
        SPDLOG_ERROR("Failed to finalize writer: {}", std::get<int>(res));
        return -1;
    } else {
        auto w = std::move(std::get<0>(res));
        w.close();
    }
    return 0;
}
