//
// Created by pavel on 25.12.2024.
//
#ifndef FILEUPLOADER_HPP
#define FILEUPLOADER_HPP

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>
#include <rfl.hpp>
#include <rfl/json/load.hpp>
#include <rfl/json/save.hpp>


#include "util.hpp"


#include "Models.hpp"
#include "Api.hpp"
#include "ThreadSafeQueue.hpp"

using recorder::models::RecordMetadata;

namespace recorder {
    struct UploadFile {
        std::filesystem::path file_path;
        RecordMetadata metadata;
    };

    class FileUploader {
    protected:
        ThreadSafeQueue<UploadFile> upload_queue_{};
        std::thread upload_thread_{};
        std::filesystem::path root_path_;
        std::shared_ptr<Api> api_{};
        bool finishing_ = false;


        void UploadLoop() {
            SPDLOG_DEBUG("UploadLoop() is running in thread {}", get_thread_id(std::this_thread::get_id()));
            while (auto file = upload_queue_.ConsumeSync()) {
                while (!finishing_ && !api_->EnsureAuthorized()) {
                    for (auto i = 0; !finishing_ && i < 60; i++) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (finishing_) {
                        break;
                    }
                    SPDLOG_WARN("Could not get api connection waiting 60 seconds");
                }
                if (finishing_) {
                    break;
                }
                if (const auto res = api_->Upload(file->file_path, file->metadata)) {
                    try {
                        auto json_path = file->file_path;
                        json_path.replace_extension(".json");
                        remove_all(json_path);
                        remove_all(file->file_path);
                    } catch (const std::filesystem::filesystem_error &e) {
                        SPDLOG_WARN("Could not remove file {} : {}", file->file_path.string(), e.what());
                    }
                } else {
                    SPDLOG_ERROR("Error uploading file: {}", res.error()->what());
                    // Recycle failed uploads
                    upload_queue_.Produce(*file);
                }
            }
        }

    public:
        std::filesystem::path &root_path() { return root_path_; }

        explicit FileUploader(const std::shared_ptr<Api> &api, const std::filesystem::path &root_path)
            : upload_thread_(std::thread(&FileUploader::UploadLoop, this)), root_path_(root_path), api_(api) {

            if (!exists(root_path)) {
                create_directory(root_path);
            } else if (!is_directory(root_path)) {
                SPDLOG_ERROR("FileUploader.root_path is not a directory");
                throw std::runtime_error("FileUploader.root_path is not a directory");
            }
            AddOldFiles();
        };

        ~FileUploader() {
            finishing_ = true;
            upload_queue_.Finish();
            if (upload_thread_.joinable()) {
                upload_thread_.join();
            }
        }

        void UploadFile(const UploadFile &file) { // NOLINT(*-convert-member-functions-to-static)
            auto json_path = file.file_path;
            json_path.replace_extension(".json");
            try {
                rfl::json::save<>(json_path.string(), file.metadata);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Could not save .json file for {}: {}", json_path.string(), e.what());
            }

            upload_queue_.Produce(file);
        }
        void AddOldFiles() {
            for (auto &entry: std::filesystem::directory_iterator(root_path_)) {
                if (entry.is_regular_file()) {
                    auto &metadata_path = entry.path();
                    if (metadata_path.extension() == ".json") {
                        auto audio_path = metadata_path;
                        audio_path.replace_extension(".ogg");
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
} // namespace recorder
#endif //FILEUPLOADER_HPP
