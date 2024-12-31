//
// Created by pavel on 27.12.2024.
//

#include "util.hpp"

#include <winhttp.h>
#include <wil/resource.h>

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#include "spdlog/fmt/bundled/format.h"

std::string hresult_to_string(int hr) {
    wil::unique_hlocal_ansistring message;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<PSTR>(&message), 0, nullptr);
    return fmt::format("{:x} : {}", hr, std::string(message.get()));
}

std::string wstringToString(const std::wstring& wideStr) {
    std::mbstate_t state = std::mbstate_t();
    const wchar_t* src = wideStr.c_str();
    size_t len = std::wcsrtombs(nullptr, &src, 0, &state) + 1;
    char* buffer = new char[len];
    std::wcsrtombs(buffer, &src, len, &state);
    std::string str(buffer);
    delete[] buffer;
    return str;
}


ProxyConfig proxy_str_parse(LPWSTR strin) {
            auto str = wstringToString(std::wstring(strin));
            auto first = str.substr(0, str.find(';'));
            auto sp1 = str.find("=");
            auto sp2 = str.find(':', sp1 + 1);
            auto host = str.substr(sp1+1, sp2-sp1-1);
            auto port = std::stoi(str.substr(sp2+1));
            return ProxyConfig { host, port, };
}

std::optional<ProxyConfig> get_proxy_config() {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig;

    if (WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig)) {
        if (proxyConfig.lpszProxy != nullptr) {
            std::wcout << L"Proxy: " << proxyConfig.lpszProxy << std::endl;
            return proxy_str_parse(proxyConfig.lpszProxy);
            GlobalFree(proxyConfig.lpszProxy);
        } else {
            std::wcout << L"No proxy configured." << std::endl;
        }

        if (proxyConfig.lpszProxyBypass != nullptr) {
            std::wcout << L"Proxy Bypass: " << proxyConfig.lpszProxyBypass << std::endl;
            return proxy_str_parse(proxyConfig.lpszProxy);
            GlobalFree(proxyConfig.lpszProxyBypass);
        }

        if (proxyConfig.lpszAutoConfigUrl != nullptr) {
            std::wcout << L"Auto Config URL: " << proxyConfig.lpszAutoConfigUrl << std::endl;
            GlobalFree(proxyConfig.lpszAutoConfigUrl);
        }
    } else {
        std::wcerr << L"Failed to get proxy settings. Error: " << GetLastError() << std::endl;
    }
    return std::nullopt;
}

struct backstage_pass {}; // now this is a dummy structure, not an object!
_Thrd_id_t std::thread::id::*get(backstage_pass); // declare fn to call

// Explicitly instantiating the class generates the fn declared above.
template class access_bypass<_Thrd_id_t std::thread::id::*, &std::thread::id::_Id, backstage_pass>;

unsigned int get_thread_id(const std::thread::id &id) { return id.*get(backstage_pass()); };
