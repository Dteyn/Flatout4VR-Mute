#include <windows.h>
#include <process.h>

#include <cstdint>

#include "mod/voip_mute.h"

namespace {

HMODULE g_module = nullptr;
INIT_ONCE g_start_once = INIT_ONCE_STATIC_INIT;

unsigned __stdcall VoipMuteThreadEntry(void* module) {
    return static_cast<unsigned>(fl4tout::VoipMuteWorker(module));
}

BOOL CALLBACK StartVoipMuteWorker(PINIT_ONCE, PVOID, PVOID*) {
    const std::uintptr_t thread = _beginthreadex(
        nullptr,
        0,
        VoipMuteThreadEntry,
        g_module,
        0,
        nullptr);
    if (thread == 0) {
        return FALSE;
    }

    CloseHandle(reinterpret_cast<HANDLE>(thread));
    return TRUE;
}

}  // namespace

extern "C" __declspec(dllexport) BOOL WINAPI FlatOut4VRMuteInitialize() {
    if (g_module == nullptr) {
        return FALSE;
    }
    return InitOnceExecuteOnce(&g_start_once, StartVoipMuteWorker, nullptr, nullptr);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    } else if (reason == DLL_PROCESS_DETACH) {
        fl4tout::RequestVoipMuteStop();
    }
    return TRUE;
}
