#include <windows.h>

#include "proxy/proxy_loader.h"

namespace {

fl4tout::proxy::ProxyLoader g_proxy_loader(L"version.dll");

template <typename T>
T Resolve(const char* name) {
    return reinterpret_cast<T>(g_proxy_loader.Resolve(name));
}

}  // namespace

extern "C" BOOL WINAPI GetFileVersionInfoA(
    LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    static const auto function = Resolve<Fn>("GetFileVersionInfoA");
    return function != nullptr ? function(filename, handle, length, data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoW(
    LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    static const auto function = Resolve<Fn>("GetFileVersionInfoW");
    return function != nullptr ? function(filename, handle, length, data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(
    DWORD flags, LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static const auto function = Resolve<Fn>("GetFileVersionInfoExA");
    return function != nullptr ? function(flags, filename, handle, length, data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(
    DWORD flags, LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static const auto function = Resolve<Fn>("GetFileVersionInfoExW");
    return function != nullptr ? function(flags, filename, handle, length, data) : FALSE;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    static const auto function = Resolve<Fn>("GetFileVersionInfoSizeA");
    return function != nullptr ? function(filename, handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    static const auto function = Resolve<Fn>("GetFileVersionInfoSizeW");
    return function != nullptr ? function(filename, handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(
    DWORD flags, LPCSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    static const auto function = Resolve<Fn>("GetFileVersionInfoSizeExA");
    return function != nullptr ? function(flags, filename, handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(
    DWORD flags, LPCWSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    static const auto function = Resolve<Fn>("GetFileVersionInfoSizeExW");
    return function != nullptr ? function(flags, filename, handle) : 0;
}

extern "C" DWORD WINAPI VerFindFileA(
    DWORD flags, LPCSTR filename, LPCSTR windows_dir, LPCSTR app_dir,
    LPSTR current_dir, PUINT current_dir_length, LPSTR destination_dir, PUINT destination_dir_length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    static const auto function = Resolve<Fn>("VerFindFileA");
    return function != nullptr
        ? function(flags, filename, windows_dir, app_dir, current_dir, current_dir_length,
                   destination_dir, destination_dir_length)
        : 0;
}

extern "C" DWORD WINAPI VerFindFileW(
    DWORD flags, LPCWSTR filename, LPCWSTR windows_dir, LPCWSTR app_dir,
    LPWSTR current_dir, PUINT current_dir_length, LPWSTR destination_dir, PUINT destination_dir_length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static const auto function = Resolve<Fn>("VerFindFileW");
    return function != nullptr
        ? function(flags, filename, windows_dir, app_dir, current_dir, current_dir_length,
                   destination_dir, destination_dir_length)
        : 0;
}

extern "C" DWORD WINAPI VerInstallFileA(
    DWORD flags, LPCSTR source_filename, LPCSTR destination_filename, LPCSTR source_dir,
    LPCSTR destination_dir, LPCSTR current_dir, LPSTR temp_file, PUINT temp_file_length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    static const auto function = Resolve<Fn>("VerInstallFileA");
    return function != nullptr
        ? function(flags, source_filename, destination_filename, source_dir, destination_dir,
                   current_dir, temp_file, temp_file_length)
        : 0;
}

extern "C" DWORD WINAPI VerInstallFileW(
    DWORD flags, LPCWSTR source_filename, LPCWSTR destination_filename, LPCWSTR source_dir,
    LPCWSTR destination_dir, LPCWSTR current_dir, LPWSTR temp_file, PUINT temp_file_length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    static const auto function = Resolve<Fn>("VerInstallFileW");
    return function != nullptr
        ? function(flags, source_filename, destination_filename, source_dir, destination_dir,
                   current_dir, temp_file, temp_file_length)
        : 0;
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD language, LPSTR buffer, DWORD size) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    static const auto function = Resolve<Fn>("VerLanguageNameA");
    return function != nullptr ? function(language, buffer, size) : 0;
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD language, LPWSTR buffer, DWORD size) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    static const auto function = Resolve<Fn>("VerLanguageNameW");
    return function != nullptr ? function(language, buffer, size) : 0;
}

extern "C" BOOL WINAPI VerQueryValueA(
    LPCVOID block, LPCSTR sub_block, LPVOID* buffer, PUINT length) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    static const auto function = Resolve<Fn>("VerQueryValueA");
    return function != nullptr ? function(block, sub_block, buffer, length) : FALSE;
}

extern "C" BOOL WINAPI VerQueryValueW(
    LPCVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT length) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static const auto function = Resolve<Fn>("VerQueryValueW");
    return function != nullptr ? function(block, sub_block, buffer, length) : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_proxy_loader.SetProxyModule(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
