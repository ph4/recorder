//
// Created by pavel on 22.03.2025.
//
module;
#include <memory>
#include <unordered_set>

#include "Controller.hpp"

export module Recorder;
import Api;
import Models;
import FileUploader;
import ProcessRecorder;
import ProcessLister;
import WinAudioSource;
import AudioSource;


struct RecorderItem {
    std::unique_ptr<recorder::ProcessRecorder<int16_t>> recorder;
    recorder::ProcessInfo process;
};

namespace recorder {
    export class Recorder {
        std::shared_ptr<models::LocalConfig> config_{};
        models::RemoteConfig remote_config_{};
        std::shared_ptr<Api> api_{};
        std::shared_ptr<FileUploader> uploader_{};
        std::shared_ptr<Controller> controller_{};
        ProcessLister process_lister_{};
        std::unordered_map<std::string, std::unique_ptr<RecorderItem>> recorders_{};
        std::unordered_set<std::string> app_whitelist_{};


        void LoadConfig();

        void Register();

        void WaitAllStopped();

        void StartRecordingOnProcess(const ProcessInfo &pi);

        void AddNewProcesses();

        void RemoveStoppedProcesses();

        bool ListenProcesses();

        void Init();

    public:
        Recorder() = default;

        void MainLoop();
    };
}