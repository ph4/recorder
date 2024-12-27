//
// Created by pavel on 02.12.2024.
//
module;
#include <optional>
#include <rfl.hpp>
#include <string>

export module Models;

namespace recorder::models {
    export struct LocalConfig {
        const std::string api_root;
        const std::string name;
        const std::string token;
        const std::optional<bool> keep_files = std::nullopt;
        const std::optional<bool> offline_mode = std::nullopt;
        // std::optional<bool> offline_files = std::nullopt;
    };

    export struct RemoteConfig {
        using Int = long;
        std::string name;
        //std::optional<bool> keep_files = std::nullopt;
        Int status_interval_s;
        Int max_silence_seconds;
        Int window_size_ms;
        double_t voice_threshold;
        Int max_recording_s;
        Int bitrate_kbps;
    };

    export struct RecordMetadata {
        // rfl::Timestamp<"%Y-%m-%dT%H:%M:%S.%f"> start_time;
        uint64_t started; // Unix timestamp
        int64_t length_seconds;
    };

    export struct Record {
    };

    export struct Register {
        std::string name;
    };

    export struct CommandBase {
        rfl::Literal<"normal", "force_upload", "reload"> type;
    };
    export struct CommandStop {
        rfl::Literal<"stop"> type;
        bool data;
    };

    export struct CommandKill {
        rfl::Literal<"kill"> type;
        int data;
    };

    export using Command = rfl::TaggedUnion<"type", CommandBase, CommandStop, CommandKill>;
}
