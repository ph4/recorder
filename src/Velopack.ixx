//
// Created by pavel on 15.03.2025.
//
module;
#include <string>

export module Velopack;

namespace recorder::velopack {
    export std::string get_update_channel();
    export std::string get_version();
    export void update_app();
    export int init_velopack();
}
