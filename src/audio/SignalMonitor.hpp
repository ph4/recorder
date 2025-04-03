//
// Created by pavel on 28.12.2024.
//
#ifndef SIGNAL_MONITOR_H
#define SIGNAL_MONITOR_H

#include <functional>
#include <chrono>

namespace recorder::audio {
    class SignalMonitorSimple {
    public:
        virtual ~SignalMonitorSimple() = default;

        virtual bool HasSignal() = 0;
    };

    struct SignalActiveData {
        std::chrono::time_point<std::chrono::system_clock> timestamp;
        std::string activationSource;
        /**
         * JSON metadata
         */
        std::optional<std::string> metadata;
    };

    struct SignalInactiveData {
    };


    struct SignalMonitorCallbacks {
        const std::function<void(SignalActiveData)> onActivated;
        const std::function<void(SignalInactiveData)> onDeactivated;

        SignalMonitorCallbacks(const std::function<void(SignalActiveData)> &onActivated,
                               const std::function<void(SignalInactiveData)> &onDeactivated)
            : onActivated(onActivated), onDeactivated(onDeactivated) {
        }
    };

    struct SignalMonitorSilenceCallbacks : SignalMonitorCallbacks {
        size_t max_silence_seconds;
        SignalMonitorSilenceCallbacks(const std::function<void(SignalActiveData)> &onActivated,
                                      const std::function<void(SignalInactiveData)> &onDeactivated,
                                      const size_t max_silence_seconds)
            : SignalMonitorCallbacks(onActivated, onDeactivated), max_silence_seconds(max_silence_seconds) {}

    };
}
#endif
