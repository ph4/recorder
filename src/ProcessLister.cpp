#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <set>
#include <unordered_set>

#include <wil/com.h>
#include <wrl/client.h>

#include "util.hpp"

#include "ProcessLister.hpp"

namespace recorder {
std::vector<DWORD> getAudioPlayingPids() {
    CoInitializeGuard coInitGuard;

    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
          __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)
    );
    if (FAILED(hr)) {
        throw HrError("Failed to create IMMDeviceEnumerator.", hr);
    }

    wil::com_ptr_nothrow<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        throw HrError("Failed to get default audio endpoint.", hr);
    }

    wil::com_ptr_nothrow<IAudioSessionManager2> sessionManager;
    hr = device->Activate(
          __uuidof(IAudioSessionManager2),
          CLSCTX_ALL,
          nullptr,
          reinterpret_cast<void **>(&sessionManager)
    );
    if (FAILED(hr)) {
        throw HrError("Failed to activate IAudioSessionManager2.", hr);
    }

    wil::com_ptr_nothrow<IAudioSessionEnumerator> sessionEnumerator;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    if (FAILED(hr)) {
        throw HrError("Failed to get IAudioSessionEnumerator.", hr);
    }

    int sessionCount = 0;
    hr = sessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        throw HrError("Failed to get session count.", hr);
    }

    std::vector<DWORD> processIDs;
    for (int i = 0; i < sessionCount; ++i) {
        wil::com_ptr_nothrow<IAudioSessionControl> sessionControl;
        hr = sessionEnumerator->GetSession(i, &sessionControl);
        if (FAILED(hr)) continue;

        wil::com_ptr_nothrow<IAudioSessionControl2> sessionControl2;
        hr = sessionControl->QueryInterface(IID_PPV_ARGS(&sessionControl2));
        if (FAILED(hr)) continue;

        DWORD sessionPID = 0;
        hr = sessionControl2->GetProcessId(&sessionPID);
        if (SUCCEEDED(hr) && sessionPID != 0) {
            processIDs.push_back(sessionPID);
        }
    }
    return processIDs;
}

std::string ProcessInfo::process_name() const { return process_name_; }

DWORD ProcessInfo::process_id() const { return process_id_; }

bool ProcessInfo::isAlive() const {
    if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id_)) {
        CloseHandle(hProcess);
        return true;
    }
    return false;
};

bool ProcessInfo::isPlayingAudio() const {
    auto pids = getAudioPlayingPids();
    return std::ranges::any_of(pids, [&](auto pid) { return pid == process_id_; });
}

std::vector<ProcessInfo> ProcessLister::getAudioPlayingProcesses() {
    const auto processIDs = getAudioPlayingPids();

    // const std::vector<DWORD> rootProcessIDs = getRootProcesses(processIDs);
    std::vector<ProcessInfo> audioProcesses;
    for (DWORD pid : processIDs) {
        std::string processName = getProcessNameByPID(pid);
        if (!processName.empty()) {
            audioProcesses.emplace_back(processName, pid);
        }
    }

    return audioProcesses;
}

std::string ProcessLister::getProcessNameByPID(const DWORD pid) {
    HANDLE const hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return "";
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == pid) {
                CloseHandle(hSnapshot);
                return pe32.szExeFile;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return "";
}

std::vector<DWORD> ProcessLister::getRootProcesses(const std::vector<DWORD> &processIDs) {
    std::map<DWORD, DWORD> parentMap;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    std::unordered_set<DWORD> allProcesses;

    if (Process32First(hSnapshot, &pe32)) {
        do {
            parentMap[pe32.th32ProcessID] = pe32.th32ParentProcessID;
            allProcesses.insert(pe32.th32ProcessID);
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);

    std::vector<DWORD> roots;
    for (const DWORD pid : processIDs) {
        DWORD current = pid;
        // Make sure that parent process is actually alive
        while (parentMap.contains(current)
               && parentMap[current] != 0
               && allProcesses.contains(parentMap[current])) {
            current = parentMap[current];
        }
        if (std::ranges::find(roots, current) == roots.end()) {
            roots.push_back(current);
        }
    }

    return roots;
}
} // namespace recorder
