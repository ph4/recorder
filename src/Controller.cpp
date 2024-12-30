//
// Created by pavel on 30.12.2024.
//
//module Controller;
#include <controller.hpp>
// import Api;
#include <mutex>
#include <variant>

#include "rfl/always_false.hpp"

using namespace std::chrono;

namespace recorder {
    models::Status Controller::GetAggregateStatus() {
        std::optional<models::StatusWithFile> best_recording;
        for (auto &[key, st]: statuses_) {
            auto visitor = [&]<typename T>(const T &s) {
                using enum InternalStatusType;
                using Type = std::decay_t<T>;
                if constexpr (std::is_same_v<Type, InternalStatusBase>) {
                    switch (s.type) {
                        case idle:
                            break;
                        default:
                            throw std::runtime_error("Unknown status type on InternalStatusBase");
                    };
                } else if constexpr (std::is_same_v<Type, InternalStatusWithMetadata>) {
                    switch (s.type) {
                        case recording: {
                            auto is_better = best_recording.has_value() &&
                                             s.metadata.length_seconds > best_recording.value().data.length_seconds;
                            if (!best_recording.has_value() || is_better) {
                                best_recording.emplace(models::StatusTypeWithFile::recording, s.metadata);
                            }
                            break;
                        }
                        default:
                            throw std::runtime_error("Unknown status type on InternalStatusWithMetadata");
                    }
                } else {
                    static_assert(rfl::always_false_v<Type>, "Not all cases were covered");
                }
            };
            st.visit(visitor);
        }
        if (best_recording.has_value()) {
            return best_recording.value();
        } else {
            return models::StatusBase{.type = models::StatusType::idle};
        }
    }

    /**
     *
     * @param api Api instance
     * @param status_interval_ms Time between sending status to the server in ms
     */
    Controller::Controller(const std::shared_ptr<Api> &api, size_t status_interval_ms)
        : thread_(std::thread(&Controller::StatusLoop, this)), api_(api), status_interval_ms_(status_interval_ms) {}


    void Controller::HandleIncomingCommand(const models::Command &command) {
        std::lock_guard cmdl(command_mutex_);
        auto visitor = [&](const auto &cmd) {
            using Type = std::decay_t<decltype(cmd)>;
            using enum models::CommandType;
            if constexpr (std::is_same_v<Type, models::CommandBase>) {
                switch (cmd.type) {
                    case normal:
                        break;
                    default:
                        throw std::runtime_error("Unknown command type on CommandBase");
                }
            }
            else if constexpr (std::is_same_v<Type, models::CommandStop>) {

            }
            else if constexpr (std::is_same_v<Type, models::CommandKill>) {
            } else {
                static_assert(rfl::always_false_v<Type>, "Unknown command type");
            }
        };
    }


    void Controller::StatusLoop() {
        while (!finishing_) {
            auto start = high_resolution_clock::now();
            if (auto apio = api_->GetRegistered()) {
                auto api = apio.value().get();
                std::lock_guard lock(status_mutex_);
                if (global_status_.load(std::memory_order_relaxed).type == models::StatusType::idle) {
                    using enum models::StatusType;
                    auto status = GetAggregateStatus();
                    auto res = api.SendStatus(status);
                    if (res) {
                        auto cmd = res.value();

                    }
                } else {
                }
            }
            auto elapsed = high_resolution_clock::now() - start;
            std::this_thread::sleep_for(milliseconds(status_interval_ms_) - elapsed);
        }
    }

    models::Command Controller::SetStatus(const std::string &name, const InternalStatus &status) {
        std::lock_guard lock(status_mutex_);
        statuses_.emplace(name, status);
        return PollCommand(name);
    }
    models::Command Controller::PollCommand(const std::string &name) {
        if (global_command_.has_value()) {
            return global_command_.value();
        }
        std::lock_guard lock(command_mutex_);
        auto cmd = commands_[name];
        commands_[name] = models::CommandBase{.type = models::CommandType::normal};
        return cmd;
    }
} // namespace recorder
