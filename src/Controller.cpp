//
// Created by pavel on 30.12.2024.
//
//module Controller;
#include <controller.hpp>
// import Api;
#include <mutex>
#include <variant>
#include <ranges>

#include <rfl/always_false.hpp>
#include <rfl/enums.hpp>

#include "logging.hpp"

using namespace std::chrono;
using namespace recorder::models;


namespace recorder {
    Status Controller::GetAggregateStatus() {
        std::optional<StatusWithFile> best_recording;
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
                                best_recording.emplace(StatusTypeWithFile::recording, s.metadata);
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
            return StatusBase{.type = StatusType::idle};
        }
    }

    void Controller::HandleIncomingCommand(const models::Command &command) {
        std::lock_guard cmdl(command_mutex_);
        auto command_visitor = [&](const auto &command) {
            using Type = std::decay_t<decltype(command)>;
            using enum CommandType;

            if constexpr (std::is_same_v<Type, CommandBase>) {
                switch (command.type) {
                    case normal:
                        break;
                    case force_upload:
                        for (auto &k : std::views::keys(commands_) ) {
                            commands_.emplace(k, force_upload);
                        }
                        break;
                    case reload: {
                        auto global_cmd_setter = [&](const auto &c) {
                            switch (c.type) {
                                case kill:
                                case stop:
                                case force_upload:
                                    break;
                                default:
                                    global_command_ = command;
                            }
                        };
                        if (global_command_.has_value()) {
                            global_command_.value().visit(global_cmd_setter);
                        } else {
                            global_command_ = command;
                        }
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown command type on CommandBase");
                }
            }
            else if constexpr (std::is_same_v<Type, CommandStop>) {
                auto global_cmd_setter = [&]<typename T>(const T &c) {
                    using Type = std::decay_t<T>;
                    switch (c.type) {
                        case kill:
                            break;
                        case stop:
                            if constexpr (std::is_same_v<Type, CommandStop>) {
                                if (command.data && !c.data) {
                                    global_command_ = command;
                                }
                            }
                        default:
                            global_command_ = command;
                    }
                };
                if (global_command_.has_value()) {
                    global_command_.value().visit(global_cmd_setter);
                } else {
                    global_command_ = command;
                }
            }
            else if constexpr (std::is_same_v<Type, CommandKill>) {
                auto global_cmd_setter = [&]<typename T>(const T &c) {
                    using Type = std::decay_t<T>;
                    if constexpr (std::is_same_v<Type, CommandKill>) {
                        if (c.data > command.data)
                            global_command_ = command;
                    } else {
                        global_command_ = command;
                    }
                };
                if (global_command_.has_value()) {
                    global_command_.value().visit(global_cmd_setter);
                } else {
                    global_command_ = command;
                }
            } else {
                static_assert(rfl::always_false_v<Type>, "Unknown command type");
            }
        };
        command.visit(command_visitor);
    }

    /**
     *
     * @param api Api instance
     * @param status_interval_ms Time between sending status to the server in ms
     */
    Controller::Controller(const std::shared_ptr<Api> &api, size_t status_interval_ms)
        : thread_(std::thread(&Controller::StatusLoop, this)), api_(api), status_interval_ms_(status_interval_ms) {}

    Controller::~Controller() {
        finishing_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void Controller::StatusLoop() {
        while (!finishing_) {
            auto start = high_resolution_clock::now();
            if (auto apio = api_->GetRegistered()) {
                if (finishing_) {
                    break;
                }
                auto api = apio.value().get();
                std::lock_guard lock(status_mutex_);
                auto global_status = global_status_.load(std::memory_order_relaxed);
                switch (global_status.type) {
                    case StatusType::idle: {
                        auto status = GetAggregateStatus();
                        status.visit([&]<typename T>(const T &s) {
                            using Type = std::decay_t<T>;
                            if constexpr (std::is_same_v<Type, StatusBase>) {
                                if (s.type == StatusType::idle) {
                                    SPDLOG_TRACE("Sending status: {}", rfl::enum_to_string(s.type));
                                } else {
                                    SPDLOG_DEBUG("Sending status: {}", rfl::enum_to_string(s.type));
                                }
                            } else {
                                    SPDLOG_DEBUG("Sending status: {}", rfl::enum_to_string(s.type));
                            }
                        });
                        if (auto res = api.SendStatus(status)) {
                            auto cmd = res.value();
                            cmd.visit([&](const auto &s) {
                                if (s.type != CommandType::normal) {
                                    SPDLOG_INFO("Received command: {}", rfl::enum_to_string(s.type));
                                }
                            });
                            HandleIncomingCommand(cmd);
                        } else {
                            SPDLOG_ERROR("Failed to send status: {}", res.error().value().what());
                        }
                        break;
                    }
                    case StatusType::exited:
                    case StatusType::exiting:
                    case StatusType::reloading: {
                        SPDLOG_DEBUG("Status = {}, finishing", rfl::enum_to_string(global_status.type));
                        finishing_ = true;
                        continue;
                        break;
                    }
                    default:
                        SPDLOG_ERROR("Unknown status type");
                        throw std::runtime_error("Unknown status type");
                }
            }
            auto elapsed = high_resolution_clock::now() - start;
            std::this_thread::sleep_for(milliseconds(status_interval_ms_) - elapsed);
        }
    }

    Command Controller::SetStatus(const std::string &name, const InternalStatus &status) {
        std::lock_guard lock(status_mutex_);
        statuses_.insert_or_assign(name, status);
        return PollCommand(name);
    }
    Command Controller::PollCommand(const std::string &name) {
        if (global_command_.has_value()) {
            return global_command_.value();
        }
        std::lock_guard lock(command_mutex_);
        auto cmd = commands_[name];
        commands_[name] = models::CommandBase{.type = models::CommandType::normal};
        return cmd;
    }
} // namespace recorder
