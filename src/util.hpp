//
// Created by pavel on 02.12.2024.
//

#ifndef UTIL_HPP
#define UTIL_HPP
#include <windows.h>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

std::string hresult_to_string(int hr);

std::string wstringToString(const std::wstring &wideStr);

struct ProxyConfig {
    std::string host;
    int port;
};
ProxyConfig proxy_str_parse(LPWSTR strin);
std::optional<ProxyConfig> get_proxy_config();

// https://stackoverflow.com/a/15113419
template <typename type, type value, typename tag> class access_bypass {
    friend type get(tag) { return value; }
};

unsigned int get_thread_id(const std::thread::id &id);

class HrError final : public std::exception {
    const HRESULT hr_;
    const char *message_;
    const std::string what_;

  public:
    explicit HrError(const char *message, const HRESULT hr)
        : hr_(hr), message_(message), what_(std::format("{} ({:#X})", message_, hr_)) {}
    const char *what() const noexcept override { return what_.c_str(); }
};

struct CoInitializeGuard {
    CoInitializeGuard() {
        const auto hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            if (hr != RPC_E_CHANGED_MODE) {
                throw HrError("Failed to initialize COM library.", hr);
            };
        }
    }
    ~CoInitializeGuard() { CoUninitialize(); }
};

#endif // UTIL_HPP
