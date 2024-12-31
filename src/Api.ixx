//
// Created by pavel on 02.12.2024.
//

module;
#include <utility>

#include <httplib.h>
#include <rfl/Result.hpp>
#include <rfl/json/Parser.hpp>

export module Api;

import Models;

namespace recorder {
    export class ApiRegistered {
    protected:
        std::shared_ptr<models::LocalConfig> config_;
        std::string api_stem_;
        std::string api_root_;
        httplib::Headers headers_;
        bool is_authorized_ = false;

        explicit ApiRegistered(const std::shared_ptr<models::LocalConfig> &config);

        [[nodiscard]] httplib::Client &client() const;

        static rfl::Result<std::monostate> CheckConnectionError(const std::string &endpoint, const httplib::Result &res);

        bool CheckUnauthorized(const httplib::Result &res);

    public:
        rfl::Result<std::monostate> Upload(const std::filesystem::path &path, const models::RecordMetadata &metadata);

        rfl::Result<models::Command> SendStatus(const models::Status &status);
    };

    export class Api : ApiRegistered {
    public:
        explicit Api(const std::shared_ptr<models::LocalConfig> &config)
            : ApiRegistered(config) {
        }

        std::optional<std::reference_wrapper<ApiRegistered>> GetRegistered(const bool try_register = true);

        rfl::Result<models::RemoteConfig> Register();
    };
}
