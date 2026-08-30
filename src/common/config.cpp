#include "common/config.h"

#include <windows.h>

#include <fstream>

namespace fl4tout {
namespace {

constexpr char kDefaultConfigText[] =
    "; FlatOut 4 VR Mute v0.4.0\r\n"
    "; Officially supported game build: FlatOut 4: Total Insanity VR v1.87 x64.\r\n"
    "; The mod is always active while installed. Remove the mod files to disable it.\r\n"
    "; Settings are loaded once at startup. Restart the game after editing this file.\r\n"
    "\r\n"
    "[General]\r\n"
    "; 1 starts with your local voice transmission muted. Default: 0.\r\n"
    "; With AllowUnmute=1 (default), you may manually unmute yourself afterward.\r\n"
    "AlwaysMuteSelf=0\r\n"
    "\r\n"
    "; 1 automatically mutes received voice from occupied remote player slots. Default: 0.\r\n"
    "; With AllowUnmute=1 (default), you may manually unmute individual remote players afterward.\r\n"
    "AlwaysMuteOthers=0\r\n"
    "\r\n"
    "; 1 allows manual Mute/Unmute actions to override enabled AlwaysMute policies. Default: 1.\r\n"
    "; Set to 0 to keep enabled AlwaysMute policies locked and prevent accidental unmuting.\r\n"
    "AllowUnmute=1\r\n";

bool ReadBool(const std::filesystem::path& path, const wchar_t* key, bool fallback) {
    return GetPrivateProfileIntW(L"General", key, fallback ? 1 : 0, path.c_str()) != 0;
}

bool WriteDefaultConfig(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(kDefaultConfigText,
                 static_cast<std::streamsize>(sizeof(kDefaultConfigText) - 1));
    output.flush();
    return output.good();
}

}  // namespace

Config LoadConfig(const std::filesystem::path& path) {
    Config config;

    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (!exists && !exists_error) {
        config.created_default_file = WriteDefaultConfig(path);
        config.default_file_write_failed = !config.created_default_file;
    } else if (exists_error) {
        config.default_file_write_failed = true;
    }

    config.always_mute_self = ReadBool(path, L"AlwaysMuteSelf", false);
    config.always_mute_others = ReadBool(path, L"AlwaysMuteOthers", false);
    config.allow_unmute = ReadBool(path, L"AllowUnmute", true);
    return config;
}

}  // namespace fl4tout
