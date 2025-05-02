//
// Created by pavel on 26.12.2024.
//
#include "Api.hpp"

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <rfl/json/write.hpp>
#include "hwid.hpp"
#include "rfl/json/read.hpp"
#include "util.hpp"

#include "VelopackMy.hpp"

namespace recorder {
    bool Api::EnsureAuthorized() {
        if (is_authorized_) {
            return true;
        }
        const auto res = Authorize();
        if (!res.error()) {
            return true;
        }
        return false;
    }
    bool Api::IsAuthorized() const { return is_authorized_; };
    rfl::Result<std::monostate> Api::Authorize() {
        const auto body = rfl::json::write<>(models::Register{config_->name});
        auto res = client().Post(api_stem_ + "/authorize", headers_auth_, body, "application/json");
        if (const auto con = CheckConnectionError("/authorize", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            is_authorized_ = false;
            return rfl::Error(
                  std::format("Failed to authorize client: {};\n{}", res->status, res->body)
            );
        }
        SPDLOG_INFO("Authorized client: {}", res->body);
        auto r = rfl::json::read<models::Authorize>(res->body);
        if (!r.error()) {
            is_authorized_ = true;
            headers_.clear();
            headers_.emplace("Authorization", "bearer " + r.value().session_token);
        }
        return std::monostate{};
    }

    Api::Api(const std::shared_ptr<models::LocalConfig> &config)
        : config_(config),
          headers_auth_{
                std::pair("Client-Uid", get_uuid()),
                std::pair("Authorization", "bearer " + config->token),
          } {
        const auto str = std::string_view(config->api_root);
        const auto sep1 = str.find_first_of("//");
        const auto sep2 = sep1 != -1 ? str.find_first_of("/", sep1 + 2) : 0;
        api_root_ = str.substr(0, sep2);
        api_stem_ = str.substr(sep2);
    }

    [[nodiscard]] httplib::Client &Api::client() const {
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

    rfl::Result<std::monostate> Api::CheckConnectionError(
          const std::string &endpoint, const httplib::Result &res
    ) {
        if (!res) {
            SPDLOG_ERROR("Connection error ({}) : {}", endpoint, httplib::to_string(res.error()));
            return rfl::Error(to_string(res.error()));
        }
        return std::monostate{};
    }

    bool Api::CheckUnauthorized(const httplib::Result &res) {
        if (res->status == httplib::Unauthorized_401) {
            is_authorized_ = false;
            SPDLOG_WARN("API lost authorization ({})", res->location);
        }
        return false;
    }

    [[nodiscard]] rfl::Result<std::monostate> Api::SetName() const {
        const auto body = rfl::json::write<>(models::Register{
              .name = config_->name,
              .version = velopack::get_version(),
              .channel = velopack::get_update_channel(),
        });
        auto res = client().Post(api_stem_ + "/set-name", headers_, body, "application/json");
        if (const auto con = CheckConnectionError("/set-name", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            return rfl::Error(std::format("Failed to set name: {}", res->status));
        }
        return std::monostate{};
    }

    [[nodiscard]] rfl::Result<models::RemoteConfig> Api::GetConfig() const {
        auto res = client().Get(api_stem_ + "/get-config", headers_);
        if (const auto con = CheckConnectionError("/get-config", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            return rfl::Error(std::format("Failed to register client: {}", res->status));
        }
        SPDLOG_INFO("Got config : {}", res->body);
        return rfl::json::read<models::RemoteConfig>(res->body);
    }

    [[nodiscard]] rfl::Result<models::RemoteConfig> Api::Register() const {
        const auto body = rfl::json::write<>(models::Register{
              .name = config_->name,
              .version = velopack::get_version(),
              .channel = velopack::get_update_channel(),
        });
        auto res = client().Get(api_stem_ + "/register-client", headers_);
        if (const auto con = CheckConnectionError("/register-client", res); !con) {
            return con.error().value();
        }
        if (res->status != httplib::OK_200) {
            return rfl::Error(std::format("Failed to register client: {}", res->status));
        }
        SPDLOG_INFO("Got config : {}", res->body);
        return rfl::json::read<models::RemoteConfig>(res->body);
    }

    [[nodiscard]] rfl::Result<std::monostate> Api::Upload(
          const std::filesystem::path &path, const models::RecordMetadata &metadata
    ) {
        const auto ep = "/upload";
        const auto url = api_stem_ + ep;

        httplib::MultipartFormDataItems multipart;

        auto metadata_json = rfl::json::write<>(metadata);
        multipart.push_back(
              {.name = "metadata",
               .content = metadata_json,
               .filename = "",
               .content_type = "application/json"}
        );

        std::ostringstream file_content;
        file_content << std::ifstream(path.string(), std::ios::binary).rdbuf();
        multipart.push_back(
              {.name = "file",
               .content = file_content.str(),
               .filename = path.filename().string(),
               .content_type = "audio/ogg"}
        );

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

    rfl::Result<models::Command> Api::SendStatus(const models::Status &status) {
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
} // namespace recorder
