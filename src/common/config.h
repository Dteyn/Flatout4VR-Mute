#pragma once

#include <filesystem>

namespace fl4tout {

struct Config {
    bool always_mute_self = false;
    bool always_mute_others = false;
    bool allow_unmute = true;
    bool created_default_file = false;
    bool default_file_write_failed = false;
};

Config LoadConfig(const std::filesystem::path& path);

}  // namespace fl4tout
