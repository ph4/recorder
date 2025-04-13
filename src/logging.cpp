#include "logging.hpp"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>

void setup_logger() {
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/main.txt", 1024 * 1024 * 5, 5, true);
    spdlog::logger logger("main", {stdout_sink, file_sink});
    logger.flush_on(spdlog::level::trace);
    logger.set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%s] [%^%l%$] %v");
    logger.set_level(spdlog::level::trace);
    set_default_logger(std::make_shared<spdlog::logger>(logger));
}
