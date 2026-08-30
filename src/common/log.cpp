#include "common/log.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <mutex>
#include <string>

namespace fl4tout {
namespace {

std::mutex g_log_mutex;
FILE* g_log_file = nullptr;

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info: return "INFO ";
        case LogLevel::warning: return "WARN ";
        case LogLevel::error: return "ERROR";
    }
    return "?????";
}

std::string TimestampLocal() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::array<char, 32> buffer{};
    std::snprintf(
        buffer.data(), buffer.size(),
        "%04u-%02u-%02u %02u:%02u:%02u",
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond);
    return buffer.data();
}

constexpr bool DebugLoggingEnabled() {
#if defined(FL4TOUT_DEBUG_LOGGING)
    return true;
#else
    return false;
#endif
}

void FlushUnlocked() {
    if (g_log_file != nullptr) {
        std::fflush(g_log_file);
    }
}

}  // namespace

bool OpenLog(const std::filesystem::path& path) {
    std::scoped_lock lock(g_log_mutex);
    if (g_log_file != nullptr) {
        return true;
    }
    g_log_file = nullptr;
    return _wfopen_s(&g_log_file, path.c_str(), L"wb") == 0 && g_log_file != nullptr;
}

void CloseLog() {
    std::scoped_lock lock(g_log_mutex);
    if (g_log_file != nullptr) {
        FlushUnlocked();
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
}

void LogSessionHeader(std::string_view version, std::string_view target) {
    std::scoped_lock lock(g_log_mutex);
    if (g_log_file == nullptr) {
        return;
    }

    const std::string timestamp = TimestampLocal();
    std::fputs("\n============================================================\n", g_log_file);
    std::fprintf(g_log_file, "FlatOut 4 VR Mute v%.*s\n",
                 static_cast<int>(version.size()), version.data());
    std::fprintf(g_log_file, "Session started: %s (local time)\n", timestamp.c_str());
    std::fprintf(g_log_file, "Build: %s\n", DebugLoggingEnabled() ? "Debug" : "Release");
    if (!target.empty()) {
        std::fprintf(g_log_file, "Target: %.*s\n",
                     static_cast<int>(target.size()), target.data());
    }
    std::fputs("============================================================\n", g_log_file);
    FlushUnlocked();
}

void Log(LogLevel level, std::string_view message, std::string_view details) {
    if (level == LogLevel::debug && !DebugLoggingEnabled()) {
        return;
    }

    std::scoped_lock lock(g_log_mutex);
    if (g_log_file == nullptr) {
        return;
    }

    const std::string timestamp = TimestampLocal();
    std::fprintf(g_log_file, "%s | %s | %.*s",
                 timestamp.c_str(), LevelName(level),
                 static_cast<int>(message.size()), message.data());
    if (!details.empty()) {
        std::fprintf(g_log_file, " | %.*s",
                     static_cast<int>(details.size()), details.data());
    }
    std::fputc('\n', g_log_file);
    FlushUnlocked();
}

void DebugLog(std::string_view message, std::string_view details) {
    Log(LogLevel::debug, message, details);
}

}  // namespace fl4tout
