//
// Created by pavel on 31.12.2024.
//
#ifndef PROCESSLISTER_HPP
#define PROCESSLISTER_HPP

#include <windows.h>
#include <string>
#include <vector>

namespace recorder {
std::vector<DWORD> getAudioPlayingPids();

class ProcessInfo {
    std::string process_name_;
    DWORD process_id_;

public:
    ProcessInfo(std::string name, const DWORD pid)
        : process_name_(std::move(name)), process_id_(pid) {}

    [[nodiscard]] std::string process_name() const;

    [[nodiscard]] DWORD process_id() const;

    [[nodiscard]] bool isAlive() const;

    [[nodiscard]] bool isPlayingAudio() const;
};

class ProcessLister {
public:
    static std::vector<ProcessInfo> getAudioPlayingProcesses();

private:
    static std::string getProcessNameByPID(const DWORD pid);

    static std::vector<DWORD> getRootProcesses(const std::vector<DWORD> &processIDs);
};
} // namespace recorder
#endif // PROCESSLISTER_HPP
