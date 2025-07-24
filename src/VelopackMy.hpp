#pragma once

#include <string>

namespace recorder::velopack {
std::string get_update_channel();
std::string get_version();
void update_app();
int init_velopack();
} // namespace recorder::velopack
