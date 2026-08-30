#pragma once

#include <windows.h>

namespace fl4tout::proxy {

class ProxyLoader {
public:
    explicit ProxyLoader(const wchar_t* system_dll_name) noexcept;

    void SetProxyModule(HMODULE module) noexcept;
    FARPROC Resolve(const char* export_name) noexcept;

private:
    static BOOL CALLBACK InitializeOnce(PINIT_ONCE once, PVOID parameter, PVOID* context) noexcept;
    void Initialize() noexcept;
    void DebugMessage(const wchar_t* message) const noexcept;

    const wchar_t* system_dll_name_ = nullptr;
    HMODULE proxy_module_ = nullptr;
    HMODULE real_module_ = nullptr;
    INIT_ONCE initialize_once_ = INIT_ONCE_STATIC_INIT;
};

}  // namespace fl4tout::proxy
