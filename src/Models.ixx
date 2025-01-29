//
// Created by pavel on 02.12.2024.
//
module;
#include <optional>
#include <rfl.hpp>
#include <string>

export module Models;

namespace recorder::models {
    export struct Authorize {
        std::string session_token;
    };

    export struct LocalConfig {
        const std::string api_root;
        const std::string name;
        const std::string token;
        const std::optional<bool> keep_files = std::nullopt;
        const std::optional<bool> offline_mode = std::nullopt;
        // std::optional<bool> offline_files = std::nullopt;
    };

    export struct App {
        using Int = long;
        std::string exe_name;
        std::optional<std::string> module = std::nullopt;
        std::optional<Int> max_silence_seconds = std::nullopt;
        std::optional<Int> bitrate_kbps = std::nullopt;
        std::optional<Int> max_recording_s = std::nullopt;
    };

    export struct RemoteConfig {
        using Int = long;
        std::string name;
        // std::optional<bool> keep_files = std::nullopt;
        Int status_interval_s;
        Int max_silence_seconds;
        Int window_size_ms;
        double_t voice_threshold;
        Int max_recording_s;
        Int bitrate_kbps;
        std::vector<App> app_configs;
    };

    export struct RecordMetadata {
        // rfl::Timestamp<"%Y-%m-%dT%H:%M:%S.%f"> start_time;
        uint64_t started; // Unix timestamp
        int64_t length_seconds;
    };

    export struct Record {};

    export struct Register {
        std::string name;
    };

    export enum class StatusType {
        starting,
        idle,
        exiting,
        reloading,
        exited,
    };

    export enum class StatusTypeWithFile {
        recording,
        uploading,
    };

    export struct StatusBase {
        StatusType type;
    };

    export struct StatusWithFile {
        StatusTypeWithFile type;
        RecordMetadata data;
    };

    export using Status = rfl::Variant<StatusBase, StatusWithFile>;

    export enum class CommandType { normal, force_upload, reload, stop, kill };

    export struct Command {
        CommandType type;
    };
} // namespace recorder::models
