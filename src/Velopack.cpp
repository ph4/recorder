//
// Created by pavel on 15.03.2025.
//
module;
#include <Velopack.hpp>
#include "logging.hpp"

module Velopack;

namespace recorder::velopack {

    std::string update_channel = "win";
    static std::unique_ptr<Velopack::UpdateManager> update_manager;

    Velopack::UpdateManager &get_update_manager() { return *update_manager; }

    std::string get_update_channel() {
#ifdef DEBUG
        return {"debug"};
#else
        return update_channel;
# endif
    }

    std::string get_version() {
#ifdef DEBUG
        return {"0.0.0"};
#else
        auto um = update_manager.get();
        const auto upd = reinterpret_cast<vpkc_update_manager_t**>(um)[0];
        char version[255];
        vpkc_get_current_version(upd, version, sizeof(version));
        return {version};
#endif
    }
    void update_app() {
        auto &manager = update_manager;
        auto updInfo = manager->CheckForUpdates();
        if (!updInfo.has_value()) {
            return; // no updates available
        }

        // download the update, optionally providing progress callbacks
        manager->DownloadUpdates(updInfo.value());

        // prepare the Updater in a new process, and wait 60 seconds for this process to exit
        manager->WaitExitThenApplyUpdate(updInfo.value());
        exit(0); // exit the app to apply the update
    }

    int init_velopack() {
        vpkc_set_logger(
                [](void *p_user_data, const char *psz_level, const char *psz_message) {
                    spdlog::log(spdlog::level::from_str(psz_level), psz_message);
                },
                nullptr);

        bool installed;
    #ifdef DEBUG
        installed = false;
    #else
        installed = true;
        try {
            auto options = Velopack::UpdateOptions {
                .AllowVersionDowngrade = true,
                .ExplicitChannel = update_channel,
            };
            update_manager = std::make_unique<Velopack::UpdateManager>(VELOPACK_UPDATE_ROOT, &options);
            SPDLOG_INFO("update_manager->GetAppId() {}", update_manager->GetAppId());
        } catch (const std::exception &e) {
            SPDLOG_ERROR(e.what());
            return EXIT_FAILURE;
        }
    #endif
        Velopack::VelopackApp::Build()
                .Run();
        if (installed) {
            update_app();
        }
        return EXIT_SUCCESS;
    }

} // namespace velo
