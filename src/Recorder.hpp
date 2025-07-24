#pragma once

#include <memory>
#include <unordered_set>

#include "Controller.hpp"

#include "Api.hpp"
#include "FileUploader.hpp"
#include "Models.hpp"
#include "ProcessLister.hpp"
#include "ProcessRecorder.hpp"

struct RecorderItem {
    std::unique_ptr<recorder::ProcessRecorder<int16_t>> recorder;
    recorder::ProcessInfo process;
};

namespace recorder {
class Recorder {
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

    void RemoveAll();

    void StartListeningProcess(const ProcessInfo &pi);

    void StartListeningWhatsapp();

    void AddNewProcesses();

    void RemoveStoppedProcesses();

public:
    Recorder() = default;

    void Init();

    bool ListenProcesses();
};
} // namespace recorder
