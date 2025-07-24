#pragma once

#include <httplib.h>
#include <rfl/Result.hpp>
#include <rfl/json/Parser.hpp>

#include "Models.hpp"

namespace recorder {
class Api {
public:
    std::shared_ptr<models::LocalConfig> config_;
    std::string api_stem_;
    std::string api_root_;
    httplib::Headers headers_;
    httplib::Headers headers_auth_;
    bool is_authorized_ = false;

    explicit Api(const std::shared_ptr<models::LocalConfig> &config);

    [[nodiscard]] httplib::Client &client() const;

    static rfl::Result<std::monostate> CheckConnectionError(
          const std::string &endpoint, const httplib::Result &res
    );

    bool CheckUnauthorized(const httplib::Result &res);

public:
    bool EnsureAuthorized();
    bool IsAuthorized() const;
    rfl::Result<std::monostate> Authorize();

    rfl::Result<models::RemoteConfig> GetConfig() const;

    rfl::Result<std::monostate> SetName() const;

    rfl::Result<models::RemoteConfig> Register() const;

    rfl::Result<std::monostate> Upload(
          const std::filesystem::path &path, const models::RecordMetadata &metadata
    );

    rfl::Result<models::Command> SendStatus(const models::Status &status);
};
} // namespace recorder
