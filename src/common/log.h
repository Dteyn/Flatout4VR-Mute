#pragma once

#include <filesystem>
#include <string_view>

namespace fl4tout {

enum class LogLevel {
    debug,
    info,
    warning,
    error,
};

bool OpenLog(const std::filesystem::path& path);
void CloseLog();
void LogSessionHeader(std::string_view version, std::string_view target);
void Log(LogLevel level, std::string_view message, std::string_view details = {});
void DebugLog(std::string_view message, std::string_view details = {});

}  // namespace fl4tout
