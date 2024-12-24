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
import RecordManager;


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

    auto fc = recorder::audio::AudioFormat{
        .channels = 1,
        .sampleRate = 16000,
    };

    auto data_callback = [&](std::span<int16_t> data) {
    };

    auto source = recorder::audio::windows::get_source_for_pid<int16_t>(fc, data_callback, pid, true);
    auto mic = recorder::audio::windows::get_source_for_pid<int16_t>(fc, data_callback, 0, false);
    auto recorder = RecordManager<int16_t>("main", fc, std::move(mic), std::move(source), total_write_ms);
    recorder.Play();
    recorder.wire_complete().acquire();
    recorder.Stop();
    return 0;
}
