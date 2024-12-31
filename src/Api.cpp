//
// Created by pavel on 26.12.2024.
//
module;
#include <httplib.h>
#include <rfl/json/write.hpp>
#include <spdlog/spdlog.h>
#include "hwid.hpp"
#include "rfl/json/read.hpp"
#include "util.hpp"

module Api;

namespace recorder {
    std::optional<std::reference_wrapper<ApiRegistered> > Api::GetRegistered(const bool try_register) {
        if (is_authorized_) {
            return static_cast<ApiRegistered &>(*this);
        } else if (try_register) {
            if (auto success = Register()) {
                return static_cast<ApiRegistered &>(*this);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]]
    rfl::Result<models::RemoteConfig> Api::Register() {
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
        return rfl::json::read<models::RemoteConfig>(res->body);
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
        thread_local auto client = [this] {
            httplib::Client client(api_root_);
            if (auto proxy = get_proxy_config()) {
                auto [host, port] = proxy.value();
                client.set_proxy(host, port);
            }
            return client;
        }();
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

    [[nodiscard]]
    rfl::Result<std::monostate> ApiRegistered::Upload(const std::filesystem::path &path,
                                                      const models::RecordMetadata &metadata) {
        const auto ep = "/upload";
        const auto url = api_stem_ + ep;

        httplib::MultipartFormDataItems multipart;

        auto metadata_json = rfl::json::write<>(metadata);
        multipart.push_back(
                {.name = "metadata", .content = metadata_json, .filename = "", .content_type = "application/json"});

        std::ostringstream file_content;
        file_content << std::ifstream(path.string()).rdbuf();
        multipart.push_back({.name = "file",
                             .content = file_content.str(),
                             .filename = path.filename().string(),
                             .content_type = "audio/ogg"});


        auto res = client().Post(url, headers_, multipart);

        if (const auto con = CheckConnectionError(ep, res); !con) {
            return con.error().value();
        }
        if (res->status < 200 || res->status >= 300) {
            CheckUnauthorized(res);
            return rfl::Error(std::format("Upload failed: {}\n{}", res->status, res->body));
        }
        return std::monostate{};
    }

    rfl::Result<models::Command> ApiRegistered::SendStatus(const models::Status &status) {

        const auto ep = "/post_status";
        const auto url = api_stem_ + ep;

        const auto status_json = rfl::json::write<>(status);
        auto res = client().Post(url, headers_, status_json, "application/json");

        if (const auto con = CheckConnectionError(ep, res); !con) {
            return con.error().value();
        }
        if (res->status < 200 || res->status >= 300) {
            CheckUnauthorized(res);
            return rfl::Error(std::format("SendStatus failed: {}\n{}", res->status, res->body));
        }
        return rfl::json::read<models::Command>(res->body.c_str());
    }
}

