#include "mod/voip_mute.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

#include "common/config.h"
#include "common/log.h"
#include "common/path_utils.h"
#include "common/sha256.h"

#ifndef FL4TOUT_VERSION
#define FL4TOUT_VERSION "dev"
#endif

namespace fl4tout {
namespace {

// FlatOut 4: Total Insanity VR v1.87 x64 is the only supported runtime target.
constexpr char kExpectedSha256[] =
    "0ec9c645f3cb17e3921d54a5cef58729f077bf92e4c501a72b2acc67c5da5a9c";
constexpr std::uintmax_t kExpectedFileSize = 18'359'296;

// Compile-time; not user-configurable (see docs/TECHNICAL.md).
constexpr DWORD kHookPollIntervalMs = 250;
constexpr DWORD kAlwaysMuteEnforcementIntervalMs = 500;
constexpr DWORD kIdleWorkerIntervalMs = 1000;

constexpr std::size_t kVoiceChatMutePlayerVtableIndex = 116;
constexpr std::size_t kVoiceChatIsPlayerMutedVtableIndex = 117;
constexpr std::size_t kVoiceChatTogglePlayerMutedVtableIndex = 118;
constexpr std::size_t kShowPlayerProfileVtableIndex = 119;

// Two statically verified v1.87 derived NetworkManager tables. Runtime Flat2VR
// testing uses 0x5E15A0; 0xBBD980 is the alternate verified profile handler.
constexpr std::uintptr_t kActivePlatformShowPlayerProfileRva = 0x005E15A0;
constexpr std::uintptr_t kAlternateRaknetShowPlayerProfileRva = 0x00BBD980;
constexpr std::uintptr_t kBaseShowPlayerProfileRva = 0x0032F480;

// v1.87 Steam/RakNet capture path. The discard gate runs after GetVoice drains audio.
constexpr std::uintptr_t kVoiceCaptureRoutineRva = 0x00BDCA60;
constexpr std::array<std::uint8_t, 16> kVoiceCaptureExpectedPrologue = {
    0x40, 0x55, 0x53, 0x41, 0x56, 0x48, 0x8D, 0xAC,
    0x24, 0x20, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
};
constexpr std::uintptr_t kVoiceDiscardGateRva = 0x00BDCB5B;
constexpr std::array<std::uint8_t, 8> kVoiceDiscardExpectedBytes = {
    0x85, 0xC0,                    // TEST EAX,EAX
    0x0F, 0x85, 0x40, 0x01, 0x00, 0x00,  // JNE FlatOut cleanup path
};
constexpr std::uint8_t kVoiceDiscardMutedOpcode = 0xFF;  // 85 C0 -> FF C0 (INC EAX)
constexpr std::uintptr_t kVoiceSendCallRva = 0x00BDCC94;
constexpr std::array<std::uint8_t, 6> kVoiceSendExpectedBytes = {
    0xFF, 0x90, 0x98, 0x00, 0x00, 0x00,
};

constexpr char kGetInstanceName[] =
    "?GetInstance@NetworkManager@Game@PlayAll@@SAPEAV123@XZ";
constexpr char kGetMaxPlayerSlotsName[] =
    "?GetMaxPlayerSlots@NetworkManager@Game@PlayAll@@QEBAHXZ";
constexpr char kSlotGetIndexLocalName[] =
    "?SlotGetIndexLocal@NetworkManager@Game@PlayAll@@UEBAHXZ";
constexpr char kSlotIsOccupiedName[] =
    "?SlotIsOccupied@NetworkManager@Game@PlayAll@@UEBA_NH@Z";
constexpr char kVoiceChatMutePlayerName[] =
    "?VoiceChatMutePlayer@NetworkManager@Game@PlayAll@@UEAAXH_N@Z";
constexpr char kVoiceChatIsPlayerMutedName[] =
    "?VoiceChatIsPlayerMuted@NetworkManager@Game@PlayAll@@UEBA_NH@Z";
constexpr char kVoiceChatTogglePlayerMutedName[] =
    "?VoiceChatTogglePlayerMuted@NetworkManager@Game@PlayAll@@UEAAXH@Z";


constexpr std::array<std::uintptr_t, 2> kAcceptedShowPlayerProfileRvas = {
    kActivePlatformShowPlayerProfileRva,
    kAlternateRaknetShowPlayerProfileRva,
};

static_assert(kVoiceChatIsPlayerMutedVtableIndex == kVoiceChatMutePlayerVtableIndex + 1);
static_assert(kVoiceChatTogglePlayerMutedVtableIndex == kVoiceChatMutePlayerVtableIndex + 2);
static_assert(kShowPlayerProfileVtableIndex == kVoiceChatMutePlayerVtableIndex + 3);
static_assert(kVoiceDiscardExpectedBytes[0] == 0x85 &&
              kVoiceDiscardExpectedBytes[1] == 0xC0 &&
              kVoiceDiscardExpectedBytes[2] == 0x0F &&
              kVoiceDiscardExpectedBytes[3] == 0x85);
static_assert(kVoiceSendExpectedBytes[0] == 0xFF &&
              kVoiceSendExpectedBytes[1] == 0x90);
static_assert(!kAcceptedShowPlayerProfileRvas.empty());

using GetInstanceFn = void* (*)();
using GetMaxPlayerSlotsFn = int (*)(void* manager);
using SlotGetIndexLocalFn = int (*)(void* manager);
using SlotIsOccupiedFn = bool (*)(void* manager, int slot);
using VoiceChatMutePlayerFn = void (*)(void* manager, int slot, bool muted);
using VoiceChatIsPlayerMutedFn = bool (*)(void* manager, int slot);
using VoiceChatTogglePlayerMutedFn = void (*)(void* manager, int slot);

struct GameApi {
    GetInstanceFn get_instance = nullptr;
    GetMaxPlayerSlotsFn get_max_player_slots = nullptr;
    SlotGetIndexLocalFn slot_get_index_local = nullptr;
    SlotIsOccupiedFn slot_is_occupied = nullptr;
    VoiceChatMutePlayerFn voice_chat_mute_player = nullptr;
    VoiceChatIsPlayerMutedFn voice_chat_is_player_muted = nullptr;
    VoiceChatTogglePlayerMutedFn voice_chat_toggle_player_muted = nullptr;
};

GameApi g_api{};
std::atomic_bool g_stop_requested{false};
std::atomic_bool g_always_mute_self{false};
std::atomic_bool g_always_mute_others{false};
std::atomic_bool g_allow_unmute{false};
std::atomic_bool g_self_unmute_override{false};
std::atomic_bool g_hook_installed{false};
std::atomic_bool g_transmit_muted{false};
struct HookLoopLogGate {
    std::atomic_bool waiting_for_manager{false};
    std::atomic_bool base_manager_pending{false};
    std::atomic_bool unexpected_target{false};
    std::atomic_bool context_mismatch{false};
    std::atomic_bool always_mute_others_enforced_once{false};
};

HookLoopLogGate g_hook_log{};
std::mutex g_remote_unmute_mutex;
std::unordered_set<int> g_remote_unmute_overrides;
HMODULE g_executable = nullptr;
std::uint8_t* g_voice_discard_gate = nullptr;
std::uint8_t* g_voice_send_call = nullptr;

std::uintptr_t AddressRva(const void* pointer) {
    if (g_executable == nullptr || pointer == nullptr) {
        return 0;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(g_executable);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= base ? address - base : 0;
}

std::string RvaText(const void* pointer) {
    const auto rva = AddressRva(pointer);
    if (rva == 0) {
        return "unavailable";
    }
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << rva;
    return text.str();
}

std::string HexU32(std::uintptr_t value) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << value;
    return text.str();
}

template <typename T>
bool ResolveExport(HMODULE module, const char* name, T* destination) {
    if (module == nullptr || name == nullptr || destination == nullptr) {
        return false;
    }
    FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
        return false;
    }
    *destination = reinterpret_cast<T>(proc);
    return true;
}

bool ResolveGameApi(HMODULE executable) {
    bool ok = true;
    ok &= ResolveExport(executable, kGetInstanceName, &g_api.get_instance);
    ok &= ResolveExport(executable, kGetMaxPlayerSlotsName, &g_api.get_max_player_slots);
    ok &= ResolveExport(executable, kSlotGetIndexLocalName, &g_api.slot_get_index_local);
    ok &= ResolveExport(executable, kSlotIsOccupiedName, &g_api.slot_is_occupied);
    ok &= ResolveExport(executable, kVoiceChatMutePlayerName, &g_api.voice_chat_mute_player);
    ok &= ResolveExport(executable, kVoiceChatIsPlayerMutedName,
                        &g_api.voice_chat_is_player_muted);
    ok &= ResolveExport(executable, kVoiceChatTogglePlayerMutedName,
                        &g_api.voice_chat_toggle_player_muted);
    return ok;
}

bool IsReadablePointer(const void* pointer, std::size_t bytes) {
    if (pointer == nullptr || bytes == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT) {
        return false;
    }
    if ((info.Protect & PAGE_GUARD) != 0 || info.Protect == PAGE_NOACCESS) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto region_end =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return start <= region_end && bytes <= (region_end - start);
}

bool IsExecutablePointer(const void* pointer, std::size_t bytes) {
    if (!IsReadablePointer(pointer, bytes)) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    const DWORD protection = info.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

bool ValidateSupportedBuildLayout() {
    if (g_executable == nullptr) {
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_executable);
    if (!IsReadablePointer(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const std::uint8_t*>(g_executable) + dos->e_lfanew);
    if (!IsReadablePointer(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    const std::uintptr_t image_size = nt->OptionalHeader.SizeOfImage;
    if (image_size == 0) {
        return false;
    }

    const auto rva_in_image = [image_size](std::uintptr_t rva, std::size_t bytes) {
        return rva != 0 && rva < image_size && bytes <= image_size - rva;
    };
    if (!rva_in_image(kBaseShowPlayerProfileRva, 1) ||
        !rva_in_image(kVoiceCaptureRoutineRva,
                      kVoiceCaptureExpectedPrologue.size()) ||
        !rva_in_image(kVoiceDiscardGateRva,
                      kVoiceDiscardExpectedBytes.size()) ||
        !rva_in_image(kVoiceSendCallRva,
                      kVoiceSendExpectedBytes.size())) {
        return false;
    }
    for (const auto rva : kAcceptedShowPlayerProfileRvas) {
        if (!rva_in_image(rva, 1)) {
            return false;
        }
    }

    const auto base = reinterpret_cast<std::uintptr_t>(g_executable);
    if (!IsExecutablePointer(
            reinterpret_cast<const void*>(base + kBaseShowPlayerProfileRva), 1) ||
        !IsExecutablePointer(
            reinterpret_cast<const void*>(base + kVoiceCaptureRoutineRva),
            kVoiceCaptureExpectedPrologue.size()) ||
        !IsExecutablePointer(
            reinterpret_cast<const void*>(base + kVoiceDiscardGateRva),
            kVoiceDiscardExpectedBytes.size()) ||
        !IsExecutablePointer(
            reinterpret_cast<const void*>(base + kVoiceSendCallRva),
            kVoiceSendExpectedBytes.size())) {
        return false;
    }
    for (const auto rva : kAcceptedShowPlayerProfileRvas) {
        if (!IsExecutablePointer(reinterpret_cast<const void*>(base + rva), 1)) {
            return false;
        }
    }

    if (kVoiceDiscardGateRva <= kVoiceCaptureRoutineRva ||
        kVoiceSendCallRva <= kVoiceDiscardGateRva ||
        kVoiceSendCallRva - kVoiceCaptureRoutineRva > 0x800) {
        return false;
    }

    const auto& gate = kVoiceDiscardExpectedBytes;
    const std::uint32_t rel_bits = static_cast<std::uint32_t>(gate[4]) |
        (static_cast<std::uint32_t>(gate[5]) << 8) |
        (static_cast<std::uint32_t>(gate[6]) << 16) |
        (static_cast<std::uint32_t>(gate[7]) << 24);
    const auto rel = static_cast<std::int32_t>(rel_bits);
    const auto branch_next =
        static_cast<std::int64_t>(kVoiceDiscardGateRva) + 8;
    const auto cleanup = branch_next + rel;
    if (cleanup <= static_cast<std::int64_t>(kVoiceSendCallRva) ||
        cleanup > static_cast<std::int64_t>(kVoiceCaptureRoutineRva + 0x800)) {
        return false;
    }
    return true;
}

bool ValidateTransmitRoutine() {
    if (g_executable == nullptr || !ValidateSupportedBuildLayout()) {
        return false;
    }

    auto* capture_entry = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(g_executable) + kVoiceCaptureRoutineRva);
    g_voice_discard_gate = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(g_executable) +
        kVoiceDiscardGateRva);
    g_voice_send_call = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(g_executable) + kVoiceSendCallRva);

    if (!IsReadablePointer(capture_entry,
                           kVoiceCaptureExpectedPrologue.size()) ||
        !IsReadablePointer(g_voice_discard_gate,
                           kVoiceDiscardExpectedBytes.size()) ||
        !IsReadablePointer(g_voice_send_call,
                           kVoiceSendExpectedBytes.size())) {
        return false;
    }
    for (std::size_t i = 0; i < kVoiceCaptureExpectedPrologue.size(); ++i) {
        if (capture_entry[i] != kVoiceCaptureExpectedPrologue[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kVoiceDiscardExpectedBytes.size(); ++i) {
        if (g_voice_discard_gate[i] != kVoiceDiscardExpectedBytes[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kVoiceSendExpectedBytes.size(); ++i) {
        if (g_voice_send_call[i] != kVoiceSendExpectedBytes[i]) {
            return false;
        }
    }
    return true;
}

bool SetTransmitMuted(bool muted, const char* reason) {
    const auto& expected = kVoiceDiscardExpectedBytes;
    if (g_voice_discard_gate == nullptr ||
        !IsReadablePointer(g_voice_discard_gate, expected.size())) {
        Log(LogLevel::error, "Local transmit mute failed", "Voice discard gate is unavailable");
        return false;
    }

    // Bytes 1..7 never change. Byte 0 is either 0x85 (TEST EAX,EAX) or
    // 0xFF (INC EAX). Because ModRM byte C0 is shared by both instructions,
    // the mute transition requires only one atomic byte write. Steam
    // EVoiceResult values are non-negative; INC leaves ZF clear and the
    // existing JNE takes FlatOut's cleanup path.
    for (std::size_t i = 1; i < expected.size(); ++i) {
        if (g_voice_discard_gate[i] != expected[i]) {
            Log(LogLevel::error, "Local transmit mute failed",
                "Voice discard branch no longer matches v1.87 at " + RvaText(g_voice_discard_gate));
            return false;
        }
    }

    const std::uint8_t current = g_voice_discard_gate[0];
    const std::uint8_t original = expected[0];
    if (current != original && current != kVoiceDiscardMutedOpcode) {
        Log(LogLevel::error, "Local transmit mute failed",
            "Unexpected opcode at " + RvaText(g_voice_discard_gate));
        return false;
    }

    const std::uint8_t desired = muted ? kVoiceDiscardMutedOpcode : original;
    if (current == desired) {
        g_transmit_muted.store(muted, std::memory_order_release);
        return true;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(g_voice_discard_gate, 1, PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        Log(LogLevel::error, "Local transmit mute failed",
            "VirtualProtect error=" + std::to_string(GetLastError()));
        return false;
    }

    _InterlockedExchange8(reinterpret_cast<volatile char*>(g_voice_discard_gate),
                          static_cast<char>(desired));

    DWORD ignored = 0;
    const BOOL restored =
        VirtualProtect(g_voice_discard_gate, 1, old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), g_voice_discard_gate, 1);

    const bool applied = g_voice_discard_gate[0] == desired;
    if (applied) {
        g_transmit_muted.store(muted, std::memory_order_release);
    }

    if (applied) {
        DebugLog("Local transmit gate changed",
            "muted=" + std::to_string(muted) +
            " | reason=" + (reason != nullptr ? std::string(reason) : std::string("unknown")) +
            " | discard_gate_rva=" + RvaText(g_voice_discard_gate) +
            " | send_validation_rva=" + RvaText(g_voice_send_call) +
            " | capture_rva=" + HexU32(kVoiceCaptureRoutineRva) +
            " | protection_restored=" + std::to_string(restored != FALSE));
    } else {
        Log(LogLevel::error, "Local transmit mute failed",
            "Opcode write did not persist at " + RvaText(g_voice_discard_gate));
    }
    return applied;
}

bool HasRemoteUnmuteOverride(int slot) {
    std::lock_guard<std::mutex> lock(g_remote_unmute_mutex);
    return g_remote_unmute_overrides.find(slot) != g_remote_unmute_overrides.end();
}

void SetRemoteUnmuteOverride(int slot, bool enabled) {
    std::lock_guard<std::mutex> lock(g_remote_unmute_mutex);
    if (enabled) {
        g_remote_unmute_overrides.insert(slot);
    } else {
        g_remote_unmute_overrides.erase(slot);
    }
}

void EnforceAlwaysMuteOthers() {
    if (!g_always_mute_others.load(std::memory_order_relaxed) ||
        g_api.get_instance == nullptr || g_api.get_max_player_slots == nullptr ||
        g_api.slot_get_index_local == nullptr || g_api.slot_is_occupied == nullptr ||
        g_api.voice_chat_mute_player == nullptr ||
        g_api.voice_chat_is_player_muted == nullptr) {
        return;
    }

    void* manager = g_api.get_instance();
    if (manager == nullptr) {
        return;
    }

    const int max_slots = g_api.get_max_player_slots(manager);
    if (max_slots <= 0) {
        return;
    }
    const int local_slot = g_api.slot_get_index_local(manager);
    const bool allow_unmute = g_allow_unmute.load(std::memory_order_relaxed);

    int newly_muted = 0;
    int confirmed_muted = 0;
    int manual_unmute_overrides = 0;
    for (int slot = 0; slot < max_slots; ++slot) {
        const bool occupied = g_api.slot_is_occupied(manager, slot);
        if (!occupied) {
            // Do not carry a manual unmute override into a future player who
            // happens to reuse the same network slot.
            SetRemoteUnmuteOverride(slot, false);
            continue;
        }
        if (slot == local_slot) {
            continue;
        }
        if (allow_unmute && HasRemoteUnmuteOverride(slot)) {
            ++manual_unmute_overrides;
            continue;
        }

        const bool before = g_api.voice_chat_is_player_muted(manager, slot);
        if (!before) {
            g_api.voice_chat_mute_player(manager, slot, true);
        }
        const bool after = g_api.voice_chat_is_player_muted(manager, slot);
        if (!before && after) {
            ++newly_muted;
        }
        if (after) {
            ++confirmed_muted;
        }
    }

    if (newly_muted > 0 ||
        !g_hook_log.always_mute_others_enforced_once.exchange(true)) {
        DebugLog("AlwaysMuteOthers enforcement",
            "max_slots=" + std::to_string(max_slots) +
            " | local_slot=" + std::to_string(local_slot) +
            " | newly_muted=" + std::to_string(newly_muted) +
            " | confirmed_remote_muted=" + std::to_string(confirmed_muted) +
            " | manual_unmute_overrides=" + std::to_string(manual_unmute_overrides) +
            " | allow_unmute=" + std::to_string(allow_unmute));
    }
}

void EnforceAlwaysMuteSelf() {
    if (!g_always_mute_self.load(std::memory_order_relaxed)) {
        return;
    }
    if (g_allow_unmute.load(std::memory_order_relaxed) &&
        g_self_unmute_override.load(std::memory_order_relaxed)) {
        return;
    }
    SetTransmitMuted(true, "always_mute_self_enforcement");
}

extern "C" void HookShowPlayerProfile(void* manager, int slot) {
    if (manager == nullptr || g_api.voice_chat_mute_player == nullptr ||
        g_api.voice_chat_is_player_muted == nullptr ||
        g_api.slot_get_index_local == nullptr || g_api.get_max_player_slots == nullptr) {
        Log(LogLevel::error, "Voice mute action failed", "NetworkManager API is unavailable");
        return;
    }

    const int max_slots = g_api.get_max_player_slots(manager);
    if (slot < 0 || max_slots <= 0 || slot >= max_slots) {
        Log(LogLevel::warning, "Voice mute action ignored",
            "Invalid player slot=" + std::to_string(slot));
        return;
    }

    const int local_slot = g_api.slot_get_index_local(manager);
    const bool allow_unmute = g_allow_unmute.load(std::memory_order_relaxed);
    if (slot == local_slot) {
        const bool always_mute_self =
            g_always_mute_self.load(std::memory_order_relaxed);
        if (always_mute_self && !allow_unmute) {
            const bool applied = SetTransmitMuted(true, "always_mute_self_local_action");
            const bool after = g_transmit_muted.load(std::memory_order_acquire);
            Log(applied && after ? LogLevel::info : LogLevel::warning,
                "Local microphone remains muted",
                "player_slot=" + std::to_string(slot) +
                " | AlwaysMuteSelf=On | AllowUnmute=Off");
            return;
        }

        const bool before = g_transmit_muted.load(std::memory_order_acquire);
        const bool requested = !before;
        const bool previous_override =
            g_self_unmute_override.load(std::memory_order_relaxed);
        const bool policy_override = always_mute_self && allow_unmute;
        if (policy_override) {
            // Set the exemption before opening transmission so the worker
            // cannot immediately reassert AlwaysMuteSelf during this action.
            g_self_unmute_override.store(!requested, std::memory_order_release);
        }

        const bool applied = SetTransmitMuted(requested, "self_mute_action");
        const bool after = g_transmit_muted.load(std::memory_order_acquire);
        const bool confirmed = applied && after == requested;
        if (!confirmed && policy_override) {
            g_self_unmute_override.store(previous_override, std::memory_order_release);
        }

        Log(confirmed ? LogLevel::info : LogLevel::warning,
            requested ? "Local microphone muted" : "Local microphone unmuted",
            "player_slot=" + std::to_string(slot) +
            " | confirmed=" + std::string(confirmed ? "yes" : "no"));
        DebugLog("Local mute action details",
            "before=" + std::to_string(before) +
            " | after=" + std::to_string(after) +
            " | AlwaysMuteSelf=" + std::to_string(always_mute_self) +
            " | AllowUnmute=" + std::to_string(allow_unmute) +
            " | manual_unmute_override=" +
            std::to_string(g_self_unmute_override.load(std::memory_order_relaxed)));
        return;
    }

    const bool always_mute_others =
        g_always_mute_others.load(std::memory_order_relaxed);
    if (always_mute_others && !allow_unmute) {
        if (!g_api.voice_chat_is_player_muted(manager, slot)) {
            g_api.voice_chat_mute_player(manager, slot, true);
        }
        const bool after = g_api.voice_chat_is_player_muted(manager, slot);
        Log(after ? LogLevel::info : LogLevel::warning,
            "Remote player remains muted",
            "player_slot=" + std::to_string(slot) +
            " | AlwaysMuteOthers=On | AllowUnmute=Off");
        return;
    }

    const bool before = g_api.voice_chat_is_player_muted(manager, slot);
    const bool requested = !before;
    const bool previous_override = HasRemoteUnmuteOverride(slot);
    const bool policy_override = always_mute_others && allow_unmute;
    if (policy_override) {
        // Exempt before unmuting to close the race with the 500 ms worker.
        SetRemoteUnmuteOverride(slot, !requested);
    }

    g_api.voice_chat_mute_player(manager, slot, requested);
    const bool after = g_api.voice_chat_is_player_muted(manager, slot);
    const bool confirmed = after == requested;
    if (!confirmed && policy_override) {
        SetRemoteUnmuteOverride(slot, previous_override);
    }

    Log(confirmed ? LogLevel::info : LogLevel::warning,
        requested ? "Remote player muted" : "Remote player unmuted",
        "player_slot=" + std::to_string(slot) +
        " | confirmed=" + std::string(confirmed ? "yes" : "no"));
    DebugLog("Remote mute action details",
        "before=" + std::to_string(before) +
        " | after=" + std::to_string(after) +
        " | AlwaysMuteOthers=" + std::to_string(always_mute_others) +
        " | AllowUnmute=" + std::to_string(allow_unmute) +
        " | manual_unmute_override=" +
        std::to_string(HasRemoteUnmuteOverride(slot)));
}

bool TryInstallVoiceHook() {
    if (g_hook_installed.load(std::memory_order_acquire)) {
        return true;
    }
    if (g_api.get_instance == nullptr || g_executable == nullptr) {
        return false;
    }

    void* manager = g_api.get_instance();
    if (manager == nullptr) {
        if (!g_hook_log.waiting_for_manager.exchange(true)) {
            DebugLog("Waiting for NetworkManager");
        }
        return false;
    }
    g_hook_log.waiting_for_manager.store(false, std::memory_order_relaxed);

    if (!IsReadablePointer(manager, sizeof(void*))) {
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(manager);
    const std::size_t profile_index = kShowPlayerProfileVtableIndex;
    if (!IsReadablePointer(vtable, (profile_index + 1) * sizeof(void*))) {
        Log(LogLevel::error, "Voice hook failed", "NetworkManager vtable is not readable");
        return false;
    }

    void* current_mute = vtable[kVoiceChatMutePlayerVtableIndex];
    void* current_is_muted =
        vtable[kVoiceChatIsPlayerMutedVtableIndex];
    void* current_toggle =
        vtable[kVoiceChatTogglePlayerMutedVtableIndex];
    if (current_mute != reinterpret_cast<void*>(g_api.voice_chat_mute_player) ||
        current_is_muted != reinterpret_cast<void*>(g_api.voice_chat_is_player_muted) ||
        current_toggle != reinterpret_cast<void*>(g_api.voice_chat_toggle_player_muted)) {
        if (!g_hook_log.context_mismatch.exchange(true)) {
            Log(LogLevel::error, "Voice hook failed", "NetworkManager voice vtable does not match v1.87");
        }
        return false;
    }
    g_hook_log.context_mismatch.store(false, std::memory_order_relaxed);

    void** slot_address = &vtable[profile_index];
    void* current = *slot_address;
    void* hook = reinterpret_cast<void*>(&HookShowPlayerProfile);

    if (current == hook) {
        g_hook_installed.store(true, std::memory_order_release);
        return true;
    }

    const auto executable_base = reinterpret_cast<std::uintptr_t>(g_executable);
    void* base_stub = reinterpret_cast<void*>(
        executable_base + kBaseShowPlayerProfileRva);
    if (current == base_stub) {
        if (!g_hook_log.base_manager_pending.exchange(true)) {
            DebugLog("Voice hook pending", "Base NetworkManager is active");
        }
        return false;
    }

    const std::uintptr_t current_rva = AddressRva(current);
    const bool accepted = std::find(
        kAcceptedShowPlayerProfileRvas.begin(),
        kAcceptedShowPlayerProfileRvas.end(),
        current_rva) != kAcceptedShowPlayerProfileRvas.end();
    if (!accepted) {
        if (!g_hook_log.unexpected_target.exchange(true)) {
            Log(LogLevel::error, "Voice hook failed",
                "Unexpected ShowPlayerProfile target at " + RvaText(current));
        }
        return false;
    }
    g_hook_log.base_manager_pending.store(false, std::memory_order_relaxed);
    g_hook_log.unexpected_target.store(false, std::memory_order_relaxed);

    DWORD old_protection = 0;
    if (!VirtualProtect(slot_address, sizeof(void*), PAGE_READWRITE, &old_protection)) {
        Log(LogLevel::error, "Voice hook failed",
            "VirtualProtect error=" + std::to_string(GetLastError()));
        return false;
    }

    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot_address), hook);

    DWORD ignored = 0;
    const BOOL restored =
        VirtualProtect(slot_address, sizeof(void*), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot_address, sizeof(void*));
    if (*slot_address != hook) {
        Log(LogLevel::error, "Voice hook failed", "Vtable write did not persist");
        return false;
    }

    g_hook_installed.store(true, std::memory_order_release);
    Log(LogLevel::info, "Voice controls hooked successfully");
    DebugLog("Voice hook details",
        "slot=" + std::to_string(profile_index) +
        " | original_rva=" + RvaText(current) +
        " | protection_restored=" + std::to_string(restored != FALSE));
    return true;
}

void PinThisModule() {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&VoipMuteWorker), &pinned);
}

}  // namespace

DWORD WINAPI VoipMuteWorker(LPVOID module_parameter) {
    PinThisModule();
    const auto module = static_cast<HMODULE>(module_parameter);

    std::filesystem::path directory;
    try {
        directory = ModuleDirectory(module);
    } catch (...) {
        return 1;
    }

    // Repair/create the policy INI before logging starts.
    const Config config = LoadConfig(directory / L"fl4tout_voip_mute.ini");

    if (!OpenLog(directory / L"fl4tout_voip_mute.log")) {
        return 2;
    }

    LogSessionHeader(FL4TOUT_VERSION, "FlatOut 4: Total Insanity VR v1.87 x64");

    g_always_mute_self.store(config.always_mute_self, std::memory_order_relaxed);
    g_always_mute_others.store(config.always_mute_others, std::memory_order_relaxed);
    g_allow_unmute.store(config.allow_unmute, std::memory_order_relaxed);

    if (config.created_default_file) {
        Log(LogLevel::info, "Configuration file created", "fl4tout_voip_mute.ini");
    } else if (config.default_file_write_failed) {
        Log(LogLevel::warning, "Configuration file could not be created",
            "Using in-memory defaults for this session");
    }

    Log(LogLevel::info, "Configuration",
        "AlwaysMuteSelf=" + std::string(config.always_mute_self ? "On" : "Off") +
        " | AlwaysMuteOthers=" + std::string(config.always_mute_others ? "On" : "Off") +
        " | AllowUnmute=" + std::string(config.allow_unmute ? "On" : "Off"));
    DebugLog("Runtime timing",
        "hook_poll_ms=" + std::to_string(kHookPollIntervalMs) +
        " | enforcement_ms=" + std::to_string(kAlwaysMuteEnforcementIntervalMs) +
        " | idle_ms=" + std::to_string(kIdleWorkerIntervalMs));

    std::filesystem::path executable_path;
    try {
        executable_path = ExecutablePath();
    } catch (...) {
        Log(LogLevel::error, "Unable to determine game executable path");
        Log(LogLevel::error, "Mod disabled", "No game memory was modified");
        CloseLog();
        return 3;
    }

    const auto hash = Sha256File(executable_path);
    std::error_code file_error;
    const std::uintmax_t file_size =
        std::filesystem::file_size(executable_path, file_error);
    const bool supported_build = hash.has_value() && *hash == kExpectedSha256 &&
                                 !file_error && file_size == kExpectedFileSize;
    const std::string actual_size_text =
        file_error ? "unavailable" : std::to_string(file_size) + " bytes";

    if (!supported_build) {
        Log(LogLevel::error, "Unsupported game build",
            "Expected FlatOut 4: Total Insanity VR v1.87 x64 | SHA-256=" + std::string(kExpectedSha256) +
            " | size=" + std::to_string(kExpectedFileSize) +
            " bytes | Found SHA-256=" + hash.value_or("unavailable") +
            " | size=" + actual_size_text);
        Log(LogLevel::error, "Mod disabled",
            "This release supports FlatOut 4 VR v1.87 only; no game memory was modified");
        CloseLog();
        return 3;
    }

    Log(LogLevel::info, "Game build validated",
        executable_path.filename().string() +
        " | FlatOut 4: Total Insanity VR v1.87 x64 | SHA-256=" + *hash +
        " | size=" + std::to_string(file_size) + " bytes");

    g_executable = GetModuleHandleW(nullptr);
    if (g_executable == nullptr || !ResolveGameApi(g_executable)) {
        Log(LogLevel::error, "Required game voice API unavailable",
            "The supported v1.87 executable did not expose the expected NetworkManager API");
        Log(LogLevel::error, "Mod disabled", "No hook was installed");
        CloseLog();
        return 4;
    }
    DebugLog("Game API resolved", "required_exports=7");

    const bool layout_valid = ValidateSupportedBuildLayout();
    DebugLog("v1.87 layout validation",
        "mute_slot=" + std::to_string(kVoiceChatMutePlayerVtableIndex) +
        " | profile_slot=" + std::to_string(kShowPlayerProfileVtableIndex) +
        " | accepted_profile_targets=" +
            std::to_string(kAcceptedShowPlayerProfileRvas.size()) +
        " | validated=" + std::to_string(layout_valid));
    if (!layout_valid) {
        Log(LogLevel::error, "FlatOut 4 VR v1.87 layout validation failed",
            "Executable identity matched, but the expected in-memory layout was not present");
        Log(LogLevel::error, "Mod disabled", "No hook was installed");
        CloseLog();
        return 5;
    }

    const bool transmit_valid = ValidateTransmitRoutine();
    DebugLog("Transmit path validation",
        "capture_rva=" + HexU32(kVoiceCaptureRoutineRva) +
        " | discard_gate_rva=" + HexU32(kVoiceDiscardGateRva) +
        " | send_validation_rva=" + HexU32(kVoiceSendCallRva) +
        " | validated=" + std::to_string(transmit_valid));
    if (!transmit_valid) {
        Log(LogLevel::error, "Local voice transmit path validation failed",
            "Expected v1.87 voice bytes were not present; no code was modified");
        Log(LogLevel::error, "Mod disabled", "No hook was installed");
        CloseLog();
        return 5;
    }

    Log(LogLevel::info, "Voice system validated",
        "Remote receive mute and local transmit mute are available");

    if (config.always_mute_self &&
        !SetTransmitMuted(true, "always_mute_self_startup")) {
        Log(LogLevel::error, "Could not apply AlwaysMuteSelf at startup",
            "No hook was installed");
        CloseLog();
        return 6;
    }

    Log(LogLevel::info, "Initializing multiplayer voice hook");
    while (!g_stop_requested.load(std::memory_order_relaxed)) {
        if (TryInstallVoiceHook()) {
            break;
        }
        Sleep(kHookPollIntervalMs);
    }

    if (g_hook_installed.load(std::memory_order_acquire)) {
        Log(LogLevel::info, "Mod ready",
            "Show profile now controls voice mute/unmute | visual feedback: none");
    }

    if (config.always_mute_self) {
        Log(LogLevel::info, "AlwaysMuteSelf enabled",
            std::string("Local microphone transmission will stay muted") +
            (config.allow_unmute ? " unless manually unmuted" : ""));
        EnforceAlwaysMuteSelf();
    }
    if (config.always_mute_others) {
        Log(LogLevel::info, "AlwaysMuteOthers enabled",
            std::string("Occupied remote player voice will stay muted") +
            (config.allow_unmute ? " unless manually unmuted" : ""));
        EnforceAlwaysMuteOthers();
    }

    // Module is pinned while the vtable hook points into this DLL.
    const bool has_persistent_policy =
        config.always_mute_self || config.always_mute_others;
    while (!g_stop_requested.load(std::memory_order_relaxed)) {
        if (config.always_mute_self) {
            EnforceAlwaysMuteSelf();
        }
        if (config.always_mute_others) {
            EnforceAlwaysMuteOthers();
        }
        Sleep(has_persistent_policy ? kAlwaysMuteEnforcementIntervalMs
                                    : kIdleWorkerIntervalMs);
    }

    if (g_transmit_muted.load(std::memory_order_acquire)) {
        SetTransmitMuted(false, "mod_stop_restore");
    }
    Log(LogLevel::info, "Mod stopped");
    CloseLog();
    return 0;
}

void RequestVoipMuteStop() {
    g_stop_requested.store(true, std::memory_order_relaxed);
}

}  // namespace fl4tout
