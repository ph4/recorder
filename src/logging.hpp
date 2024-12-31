//
// Created by pavel on 30.12.2024.
//

#ifndef LOGGING_HPP
#define LOGGING_HPP

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/sinks/stdout_color_sinks-inl.h>
#include <spdlog/spdlog.h>

inline void setup_logger() {
    const auto logger = spdlog::stdout_color_mt("main");
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    set_default_logger(logger);
}


#endif //LOGGING_HPP
