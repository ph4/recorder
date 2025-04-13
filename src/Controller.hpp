//
// Created by pavel on 30.12.2024.
//
#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "rfl/TaggedUnion.hpp"

#include "Api.hpp"
#include "Models.hpp"

namespace recorder {
    enum class InternalStatusType {
        idle,
        recording,
    };
    struct InternalStatusBase {
        InternalStatusType type;
    };

    struct InternalStatusWithMetadata {
        InternalStatusType type;
        models::RecordMetadata metadata;
    };

    using InternalStatus = rfl::Variant<InternalStatusBase, InternalStatusWithMetadata>;

    class Controller {
        std::mutex status_mutex_{};
        std::unordered_map<std::string, InternalStatus> statuses_{};

        std::mutex command_mutex_{};
        std::unordered_map<std::string, models::Command> commands_{};

        std::optional<models::Command> global_command_ = std::nullopt;
        std::atomic<models::StatusBase> global_status_ = models::StatusBase(models::StatusType::idle);


        std::thread thread_{};
        bool finishing_ = false;

        std::shared_ptr<Api> api_;
        size_t status_interval_ms_;

    protected:
        models::Status GetAggregateStatus();
        void HandleIncomingCommand(const models::Command &command);

    public:
        Controller(const std::shared_ptr<Api> &api, size_t status_interval_ms);
        ~Controller();
        void StatusLoop();


        void Reset();
        models::Command SetStatus(const std::string &name, const InternalStatus &status);
        models::Command PollCommand(const std::string &name);
        [[nodiscard]] models::Command GetGlobalCommand() const;
    };
} // namespace recorder

#endif //CONTROLLER_HPP
