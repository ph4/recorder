//
// Created by pavel on 15.03.2025.
//
#ifndef VELOPACK_MY_HPP
#define VELOPACK_MY_HPP

#include <string>

namespace recorder::velopack {
    std::string get_update_channel();
    std::string get_version();
    void update_app();
    int init_velopack();
} // namespace recorder::velopack
#endif // VELOPACK_MY_HPP
