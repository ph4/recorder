//
// Created by pavel on 02.12.2024.
//

#ifndef UTIL_HPP
#define UTIL_HPP
#include <windows.h>
#include <string>
#include <iostream>
#include <optional>

std::string hresult_to_string(int hr);

std::string wstringToString(const std::wstring& wideStr);

struct ProxyConfig {
    std::string host;
    int port;
};
ProxyConfig proxy_str_parse(LPWSTR strin);
std::optional<ProxyConfig> get_proxy_config();

#endif //UTIL_HPP
