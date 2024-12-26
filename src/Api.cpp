//
// Created by pavel on 26.12.2024.
//
module;
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <rfl/json/write.hpp>
#include "hwid.hpp";

module Api;

namespace recorder {
    std::optional<std::reference_wrapper<ApiRegistered> > Api::GetRegistered(const bool try_register) {
        if (is_authorized_) {
            return static_cast<ApiRegistered &>(*this);
        } else if (try_register) {
            Register();
            // ReSharper disable once CppDFAConstantConditions (it lies)
            if (is_authorized_) {
                return static_cast<ApiRegistered &>(*this);
            }
        }
        return std::nullopt;
    }

    rfl::Result<std::monostate> Api::Register() {
        const auto body = rfl::json::write<>(models::Register{config_->name});
        auto res = client().Post(api_stem_ + "/register-client", headers_, body, "application/json");
        if (const auto con = CheckConnectionError("/register-client", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            is_authorized_ = false;
            return rfl::Error(std::format("Failed to register client: {}", res->status));
        }
        SPDLOG_INFO("Registered client: {}", res->body);
        is_authorized_ = true;
        return std::monostate{};
    }

    ApiRegistered::ApiRegistered(const std::shared_ptr<models::LocalConfig> &config): config_(config),

        headers_{
            std::pair("Client-Uid", get_uuid()),
            std::pair("Authorization", "bearer " + config->token)
        } {
        const auto str = std::string_view(config->api_root);
        const auto sep1 = str.find_first_of("//");
        const auto sep2 = sep1 != -1 ? str.find_first_of("/", sep1 + 2) : 0;
        api_root_ = str.substr(0, sep2);
        api_stem_ = str.substr(sep2);
    }

    [[nodiscard]] httplib::Client &ApiRegistered::client() const {
        thread_local httplib::Client client(api_root_);
        return client;
    }

    rfl::Result<std::monostate> ApiRegistered::CheckConnectionError(const std::string &endpoint,
                                                                    const httplib::Result &res) {
        if (!res) {
            SPDLOG_ERROR("Connection error ({}) : {}", endpoint, httplib::to_string(res.error()));
            return rfl::Error(to_string(res.error()));
        }
        return std::monostate{};
    }

    bool ApiRegistered::CheckUnauthorized(const httplib::Result &res) {
        if (res->status == httplib::Unauthorized_401) {
            is_authorized_ = false;
            SPDLOG_WARN("API lost authorization ({})", res->location);
        }
        return false;
    }

    rfl::Result<std::monostate> ApiRegistered::Upload(const std::filesystem::path &path,
                                                      const models::RecordMetadata &metadata) {
        httplib::MultipartFormDataItems multipart;

        multipart.push_back({
            .name = "metadata",
            .content = rfl::json::write<>(metadata),
            .filename = "",
            .content_type = "application/json"
        });

        std::ostringstream file_content;
        file_content << std::ifstream(path.string()).rdbuf();
        multipart.push_back({
            .name = "metadata",
            .content = file_content.str(),
            .filename = path.filename().string(),
            .content_type = "audio/ogg"
        });

        auto res = client().Post(api_stem_ + "/register-client", headers_, multipart);

        if (const auto con = CheckConnectionError("register-client", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            CheckUnauthorized(res);
            return rfl::Error(std::format("Upload failed: {}", res->status));
        }
        return std::monostate{};
    }
}

