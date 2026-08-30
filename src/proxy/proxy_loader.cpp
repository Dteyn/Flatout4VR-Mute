#include "proxy/proxy_loader.h"

#include <cstddef>

namespace fl4tout::proxy {
namespace {

constexpr DWORD kPathCapacity = 32768;

std::size_t BoundedLength(const wchar_t* text, std::size_t capacity) noexcept {
    std::size_t length = 0;
    while (length < capacity && text[length] != L'\0') {
        ++length;
    }
    return length;
}

bool AppendPathComponent(wchar_t* path, std::size_t capacity, const wchar_t* component) noexcept {
    const std::size_t path_length = BoundedLength(path, capacity);
    const std::size_t component_length = BoundedLength(component, capacity);
    if (path_length >= capacity || component_length >= capacity) {
        return false;
    }

    const bool needs_separator =
        path_length > 0 && path[path_length - 1] != L'\\' && path[path_length - 1] != L'/';
    const std::size_t required = path_length + (needs_separator ? 1 : 0) + component_length + 1;
    if (required > capacity) {
        return false;
    }

    std::size_t offset = path_length;
    if (needs_separator) {
        path[offset++] = L'\\';
    }
    for (std::size_t index = 0; index <= component_length; ++index) {
        path[offset + index] = component[index];
    }
    return true;
}

bool ModuleSiblingPath(HMODULE module, const wchar_t* filename, wchar_t* output, DWORD capacity) noexcept {
    const DWORD length = GetModuleFileNameW(module, output, capacity);
    if (length == 0 || length >= capacity) {
        return false;
    }

    std::size_t separator = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < length; ++index) {
        if (output[index] == L'\\' || output[index] == L'/') {
            separator = index;
        }
    }
    if (separator == static_cast<std::size_t>(-1)) {
        return false;
    }

    output[separator + 1] = L'\0';
    return AppendPathComponent(output, capacity, filename);
}

}  // namespace

ProxyLoader::ProxyLoader(const wchar_t* system_dll_name) noexcept
    : system_dll_name_(system_dll_name) {}

void ProxyLoader::SetProxyModule(HMODULE module) noexcept {
    proxy_module_ = module;
}

void ProxyLoader::DebugMessage(const wchar_t* message) const noexcept {
    OutputDebugStringW(L"[FlatOut4VRMute proxy] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

BOOL CALLBACK ProxyLoader::InitializeOnce(PINIT_ONCE, PVOID parameter, PVOID*) noexcept {
    auto* loader = static_cast<ProxyLoader*>(parameter);
    loader->Initialize();
    return TRUE;
}

void ProxyLoader::Initialize() noexcept {
    wchar_t real_path[kPathCapacity]{};
    const UINT system_length = GetSystemDirectoryW(real_path, kPathCapacity);
    if (system_length == 0 || system_length >= kPathCapacity ||
        !AppendPathComponent(real_path, kPathCapacity, system_dll_name_)) {
        DebugMessage(L"Could not resolve the Windows system DLL path.");
        return;
    }

    real_module_ = LoadLibraryW(real_path);
    if (real_module_ == nullptr) {
        DebugMessage(L"Could not load the matching Windows system DLL.");
        return;
    }

    wchar_t mod_path[kPathCapacity]{};
    if (!ModuleSiblingPath(proxy_module_, L"fl4tout_voip_mute.dll", mod_path, kPathCapacity)) {
        DebugMessage(L"Could not resolve the FlatOut 4 VR Mute payload path.");
        return;
    }
    if (GetFileAttributesW(mod_path) == INVALID_FILE_ATTRIBUTES) {
        DebugMessage(L"fl4tout_voip_mute.dll was not found beside the proxy.");
        return;
    }
    HMODULE payload_module = LoadLibraryW(mod_path);
    if (payload_module == nullptr) {
        DebugMessage(L"Found fl4tout_voip_mute.dll but LoadLibraryW failed.");
        return;
    }

    using InitializeFn = BOOL(WINAPI*)();
    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(payload_module, "FlatOut4VRMuteInitialize"));
    if (initialize == nullptr || !initialize()) {
        DebugMessage(L"FlatOut 4 VR Mute payload initialization failed.");
    }
}

FARPROC ProxyLoader::Resolve(const char* export_name) noexcept {
    InitOnceExecuteOnce(&initialize_once_, InitializeOnce, this, nullptr);
    if (real_module_ == nullptr) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }

    FARPROC function = GetProcAddress(real_module_, export_name);
    if (function == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
    }
    return function;
}

}  // namespace fl4tout::proxy
