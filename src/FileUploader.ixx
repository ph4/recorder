//
// Created by pavel on 25.12.2024.
//
module;
#include <memory>
#include <queue>
#include <string>
#include <filesystem>
#include <rfl.hpp>
#include <rfl/json/load.hpp>
#include <rfl/json/write.hpp>

#include <spdlog/spdlog.h>

export module FileUploader;
import ThreadSafeQueue;
import Models;
import Api;

using recorder::models::RecordMetadata;

namespace recorder {
    export struct UploadFile {
        std::filesystem::path file_path;
        RecordMetadata metadata;
    };

    export class FileUploader {
    protected:
        ThreadSafeQueue<UploadFile> upload_queue_{};
        std::thread upload_thread_{};
        std::filesystem::path root_path_;
        std::shared_ptr<Api> api_{};
        bool finishing_ = false;


        void UploadLoop() {
            SPDLOG_DEBUG("UploadLoop() is runnning in thread {}", std::this_thread::get_id());
            while (auto file = upload_queue_.ConsumeSync()) {
                std::optional<std::reference_wrapper<ApiRegistered>> apio;
                while (!finishing_ && !((apio = api_->GetRegistered()))) {
                    for (auto i = 0; !finishing_ && i < 60; i++) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (finishing_) {
                        break;
                    }
                    SPDLOG_WARN("Could not get api connection waiting 69 seconds");
                }
                if (finishing_) {
                    break;
                }
                auto api = apio.value().get();
                const auto res = api.Upload(file->file_path, file->metadata);
                if (!res) {
                    SPDLOG_ERROR("Error uploading file: {}", res.error()->what());
                    // Recycle failed uploads
                    upload_queue_.Produce(*file);
                }
            }
        }

    public:

        explicit FileUploader(const std::shared_ptr<Api> &api, const std::filesystem::path &root_path)
            : upload_thread_(std::thread(&FileUploader::UploadLoop, this)),
              root_path_(root_path),
              api_(api) {
        };

        ~FileUploader() {
            finishing_ = true;
            upload_queue_.Finish();
            if (upload_thread_.joinable()) {
                upload_thread_.join();
            }
        }

        void UploadFile(const UploadFile& file) {
            upload_queue_.Produce(file);
        }
        void AddOldFiles() {
            for (auto &entry: std::filesystem::directory_iterator(root_path_)) {
                if (entry.is_regular_file()) {
                    auto& metadata_path = entry.path();
                    if (metadata_path.extension() == ".json") {
                        auto audio_path = metadata_path.parent_path() / metadata_path.stem() / ".ogg";
                        if (exists(audio_path)) {
                            auto metadata_res = rfl::json::load<RecordMetadata>(metadata_path.string());
                            if (metadata_res.error()) {
                                SPDLOG_ERROR("Error reading record metadata {}", metadata_res.error().value().what());
                            } else {
                                struct UploadFile file{audio_path, metadata_res.value()};
                                SPDLOG_INFO("Found non-uploaded file {}", file.file_path.string());
                                upload_queue_.Produce(std::move(file));
                            }
                        }
                    }
                }
            }
        }
    };
}

