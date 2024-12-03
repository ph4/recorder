//
// Created by pavel on 02.12.2024.
//

#ifndef UTIL_HPP
#define UTIL_HPP
#include <string>
#include <wil/resource.h>

#include "spdlog/fmt/bundled/format.h"

inline std::string hresult_to_string(int hr) {
    wil::unique_hlocal_ansistring message;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<PSTR>(&message), 0, nullptr);
    return fmt::format("{:x} : {}", hr, std::string(message.get()));
}

#endif //UTIL_HPP
