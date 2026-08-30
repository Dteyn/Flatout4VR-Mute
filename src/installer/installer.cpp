#include "common/path_utils.h"
#include "common/sha256.h"
#include "installer/installer_resources.h"
#include "installer_payload_manifest.h"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kWindowTitle[] = L"FlatOut 4 VR Mute Installer";
constexpr wchar_t kGameOfficialName[] = L"FlatOut 4: Total Insanity VR";
constexpr wchar_t kGameShortName[] = L"FlatOut 4 VR";       // the game being modded
constexpr wchar_t kProductName[] = L"FlatOut 4 VR Mute";    // this mod/installer
constexpr wchar_t kAuthorName[] = L"Dteyn";
constexpr wchar_t kProjectUrl[] = L"https://github.com/Dteyn/Flatout4VR-Mute";
constexpr wchar_t kGameExeName[] = L"Flatout.exe";
constexpr wchar_t kSteamGameDirName[] = L"Project Fox";
constexpr wchar_t kPayloadName[] = L"fl4tout_voip_mute.dll";
constexpr wchar_t kConfigName[] = L"fl4tout_voip_mute.ini";
constexpr wchar_t kRuntimeLogName[] = L"fl4tout_voip_mute.log";
constexpr wchar_t kInstallerLogName[] = L"FlatOut4VR-Mute-Installer.log";
constexpr wchar_t kSupportedGameVersion[] = L"1.87";
constexpr std::uintmax_t kExpectedExeSize = 18'359'296;
constexpr char kExpectedExeSha256[] =
    "0ec9c645f3cb17e3921d54a5cef58729f077bf92e4c501a72b2acc67c5da5a9c";

struct PackageFileSpec {
    const wchar_t* package_path;
    const wchar_t* install_name;
    std::uintmax_t size;
    const char* sha256;
    const wchar_t* internal_name;
};

constexpr PackageFileSpec kProxyPackages[] = {
    {
        L"loaders\\version.dll",
        L"version.dll",
        fl4tout::installer_payload::kVersionProxySize,
        fl4tout::installer_payload::kVersionProxySha256,
        L"version",
    },
    {
        L"loaders\\winhttp.dll",
        L"winhttp.dll",
        fl4tout::installer_payload::kWinhttpProxySize,
        fl4tout::installer_payload::kWinhttpProxySha256,
        L"winhttp",
    },
};
constexpr std::size_t kPreferredProxyIndex = 0;

constexpr PackageFileSpec kMuteDllPackage{
    kPayloadName,
    kPayloadName,
    fl4tout::installer_payload::kMuteDllSize,
    fl4tout::installer_payload::kMuteDllSha256,
    L"fl4tout_voip_mute",
};
constexpr PackageFileSpec kConfigPackage{
    kConfigName,
    kConfigName,
    fl4tout::installer_payload::kConfigSize,
    fl4tout::installer_payload::kConfigSha256,
    nullptr,
};

constexpr int kButtonClose = 1004;
constexpr int kButtonRestartAsAdministrator = 1005;
constexpr int kButtonNotNow = 1006;
constexpr int kWizardWidth = 760;
constexpr int kWizardHeight = 535;
constexpr int kSidebarWidth = 205;
constexpr int kBottomBarHeight = 66;

// --- Shared content column ---
// Shared content column used by every page (left/right edges, indents).
constexpr int kContentLeft = 236;
constexpr int kContentRight = 726;
constexpr int kContentWidth = kContentRight - kContentLeft;
constexpr int kDescIndent = 26;
constexpr int kDescLeft = kContentLeft + kDescIndent;
constexpr int kDescWidth = kContentWidth - kDescIndent;

// --- Page header ---
constexpr int kTitleY = 32;
constexpr int kTitleH = 32;
constexpr int kSubtitleY = 72;
constexpr int kSubtitleH = 32;
constexpr int kHeaderRuleY = 112;
constexpr int kBodyTopY = 136;

// --- Welcome page ---
constexpr int kWelcomeCompatLabelY = 240;
constexpr int kWelcomeCompatValueY = 262;
constexpr int kWelcomeDetectedLabelY = 304;
constexpr int kWelcomeDetectedValueY = 326;
constexpr int kWelcomeAuthorY = 392;
constexpr int kWelcomeGithubY = 414;
constexpr int kWelcomeBodyH = kWelcomeCompatLabelY - kBodyTopY;

// --- Game folder page ---
constexpr int kBrowseW = 90;
constexpr int kFieldGap = 12;
constexpr int kFieldH = 28;
constexpr int kLocationFieldY = kBodyTopY + 24;
constexpr int kLocationStatusY = 216;

// --- Maintenance / Settings option lists ---
constexpr int kOptionRowH = 24;
constexpr int kOptionDescGap = 26;
constexpr int kOptionDescH = 42;
constexpr int kOptionBlockH = 90;

constexpr int OptionRowY(int index) {
    return kBodyTopY + index * kOptionBlockH;
}

constexpr int OptionDescY(int index) {
    return OptionRowY(index) + kOptionDescGap;
}

// --- Sidebar ---
constexpr int kBrandY = 68;
constexpr int kBrandH = 70;
constexpr int kVersionY = 150;
constexpr int kVersionH = 20;
constexpr int kSidebarRuleY = 188;
constexpr int kStepsStartY = 214;
constexpr int kStepPitch = 31;

constexpr COLORREF kSidebarTextColor = RGB(31, 27, 20);
constexpr COLORREF kSidebarSubtleTextColor = RGB(36, 29, 17);
constexpr COLORREF kSidebarInactiveStepColor = RGB(120, 96, 48);
constexpr COLORREF kSidebarInactiveDotColor = RGB(247, 213, 121);
constexpr COLORREF kSidebarRuleColor = RGB(137, 88, 0);
constexpr COLORREF kHeaderRuleColor = RGB(225, 227, 230);
constexpr COLORREF kBottomRuleColor = RGB(218, 221, 225);

// --- Navigation ---
constexpr int kButtonH = 28;
constexpr int kButtonY = kWizardHeight - kBottomBarHeight + (kBottomBarHeight - kButtonH) / 2;
constexpr int kButtonGap = 6;
constexpr int kCancelGap = 24;
constexpr int kBackW = 90;
constexpr int kNextW = 90;
constexpr int kCancelW = 84;
constexpr int kCancelX = kContentRight - kCancelW;
constexpr int kNextX = kCancelX - kCancelGap - kNextW;
constexpr int kBackX = kNextX - kButtonGap - kBackW;

struct PatchSpec {
    const wchar_t* filename;
    const wchar_t* language;
    std::uint64_t offset;
    const wchar_t* original;
    const wchar_t* replacement;
};

constexpr PatchSpec kPatchSpecs[] = {
    {L"LOCALISATION_CH_S.PLOC", L"Chinese (Simplified)", 0x0D600, L"显示档案", L"切换静音"},
    {L"LOCALISATION_CH_T.PLOC", L"Chinese (Traditional)", 0x0D456, L"顯示設定檔", L"切換靜音"},
    {L"LOCALISATION_EN.PLOC", L"English", 0x2373A, L"Show profile", L"Mute/Unmute"},
    {L"LOCALISATION_FR.PLOC", L"French", 0x27980, L"Montrer le profil", L"Muet/non muet"},
    {L"LOCALISATION_GE.PLOC", L"German", 0x2939E, L"Profil anzeigen", L"Stumm an/aus"},
    {L"LOCALISATION_IT.PLOC", L"Italian", 0x26ABE, L"Mostra profilo", L"Muto sì/no"},
    {L"LOCALISATION_JA.PLOC", L"Japanese", 0x12AAE, L"プロフィール表示", L"ミュート切替"},
    {L"LOCALISATION_KO.PLOC", L"Korean", 0x14248, L"프로필 표시", L"음소거 전환"},
    {L"LOCALISATION_POL.PLOC", L"Polish", 0x280A2, L"Pokaż profil", L"Wycisz/Włącz"},
    {L"LOCALISATION_POR_B.PLOC", L"Portuguese (Brazil)", 0x2672E, L"Exibir perfil", L"Mudo: sim/não"},
    {L"LOCALISATION_RU.PLOC", L"Russian", 0x2589A, L"Показать профиль", L"Выкл./вкл. звук"},
    {L"LOCALISATION_SP.PLOC", L"Spanish", 0x27C34, L"Mostrar perfil", L"Silencio sí/no"},
};

enum class FileState {
    Missing,
    Original,
    Patched,
    Unexpected,
    TooSmall,
    ReadError,
};

struct Target {
    fs::path path;
    const PatchSpec* spec;
};

struct ScanResult {
    std::vector<Target> targets;
    std::vector<std::wstring> problem_files;
    std::size_t original_count = 0;
    std::size_t patched_count = 0;
};

enum class RequestedAction {
    Install,
    Uninstall,
};



enum class ActionResult {
    Completed,
    Retry,
    RestartAsAdministrator,
};

enum class ComponentState {
    Missing,
    Current,
    RecognizedMod,
    Foreign,
};

struct ComponentInfo {
    ComponentState state = ComponentState::Missing;
    std::uintmax_t size = 0;
    std::string sha256;
};

struct GameValidation {
    bool found = false;
    bool supported = false;
    std::uintmax_t size = 0;
    std::string sha256;
    std::wstring problem;
};

struct PackageValidation {
    bool valid = false;
    fs::path source_dir;
    std::wstring problem;
};

struct InstallState {
    std::array<ComponentInfo, std::size(kProxyPackages)> proxies{};
    ComponentInfo payload;
    std::optional<std::size_t> installed_proxy_index;
    std::optional<std::size_t> selected_proxy_index;
    bool config_present = false;
    bool has_any_mod_component = false;
    bool has_noncurrent_mod_component = false;
    bool has_foreign_conflict = false;
};

FILE* g_installer_log = nullptr;
fs::path g_installer_log_path;
bool g_access_denied_during_action = false;

bool IsPermissionError(DWORD error) {
    return error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD;
}

void RecordWriteFailure(DWORD error) {
    if (IsPermissionError(error)) {
        g_access_denied_during_action = true;
    }
}

void RecordWriteFailure(const std::error_code& error) {
    if (error && (error == std::errc::permission_denied ||
                  IsPermissionError(static_cast<DWORD>(error.value())))) {
        g_access_denied_during_action = true;
    }
}

void ResetActionWriteState() {
    g_access_denied_during_action = false;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(
                              token, TokenElevation, &elevation,
                              sizeof(elevation), &size) != FALSE &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool ShouldRestartAsAdministrator() {
    return g_access_denied_during_action && !IsProcessElevated();
}

std::wstring LocalTimestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);
    return buffer;
}

void LogLine(std::wstring_view level, std::wstring_view message) {
    if (g_installer_log == nullptr) {
        return;
    }

    std::wstring clean(message);
    for (wchar_t& ch : clean) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            ch = L' ';
        }
    }

    const std::wstring timestamp = LocalTimestamp();
    fwprintf(
        g_installer_log,
        L"%ls | %.*ls | %ls\n",
        timestamp.c_str(),
        static_cast<int>(level.size()),
        level.data(),
        clean.c_str());
    fflush(g_installer_log);
}

void LogInfo(std::wstring_view message) {
    LogLine(L"INFO ", message);
}

void LogWarn(std::wstring_view message) {
    LogLine(L"WARN ", message);
}

void LogError(std::wstring_view message) {
    LogLine(L"ERROR", message);
}

fs::path InstallerLogPath() {
    try {
        return fl4tout::ExecutablePath().parent_path() / kInstallerLogName;
    } catch (...) {
        wchar_t temp[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, temp) > 0) {
            return fs::path(temp) / kInstallerLogName;
        }
        return fs::path(kInstallerLogName);
    }
}

void OpenInstallerLog(bool append = false, bool continuation_header = true) {
    g_installer_log_path = InstallerLogPath();
    g_installer_log = nullptr;
    _wfopen_s(&g_installer_log, g_installer_log_path.c_str(), append ? L"ab" : L"wb");
    if (g_installer_log == nullptr) {
        wchar_t temp[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, temp) > 0) {
            g_installer_log_path = fs::path(temp) / kInstallerLogName;
            _wfopen_s(&g_installer_log, g_installer_log_path.c_str(), append ? L"ab" : L"wb");
        }
    }

    if (g_installer_log != nullptr) {
        if (append && continuation_header) {
            fwprintf(g_installer_log, L"\n------------------------------------------------------------\n");
            fwprintf(g_installer_log, L"Administrator continuation: %ls (local time)\n", LocalTimestamp().c_str());
            fwprintf(g_installer_log, L"------------------------------------------------------------\n");
        } else if (!append) {
            fwprintf(g_installer_log, L"============================================================\n");
            fwprintf(g_installer_log, L"%ls Installer v%hs\n", kProductName, FL4TOUT_VERSION);
            fwprintf(g_installer_log, L"Session started: %ls (local time)\n", LocalTimestamp().c_str());
            fwprintf(g_installer_log, L"============================================================\n");
        }
        fflush(g_installer_log);
    }
}

void CloseInstallerLog() {
    if (g_installer_log != nullptr) {
        fflush(g_installer_log);
        fclose(g_installer_log);
        g_installer_log = nullptr;
    }
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size > 0) {
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    const int ansi_size = MultiByteToWideChar(CP_ACP, 0, text.data(),
                                               static_cast<int>(text.size()), nullptr, 0);
    if (ansi_size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(ansi_size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), ansi_size);
    return result;
}

std::vector<std::uint8_t> Utf16LeBytes(const std::wstring& text, bool include_null) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve((text.size() + (include_null ? 1 : 0)) * 2);
    for (const wchar_t ch : text) {
        const auto value = static_cast<std::uint16_t>(ch);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }
    if (include_null) {
        bytes.push_back(0);
        bytes.push_back(0);
    }
    return bytes;
}

std::vector<std::uint8_t> OriginalBytes(const PatchSpec& spec) {
    return Utf16LeBytes(spec.original, true);
}

std::vector<std::uint8_t> PatchedBytes(const PatchSpec& spec) {
    const std::wstring original(spec.original);
    const std::wstring replacement(spec.replacement);
    if (replacement.size() > original.size()) {
        return {};
    }

    std::wstring padded = replacement;
    padded.append(original.size() - replacement.size(), L' ');
    return Utf16LeBytes(padded, true);
}

bool ReadFileBytes(const fs::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    return input.good() || input.eof();
}

bool WriteBytesAtOffset(
    const fs::path& path,
    std::uint64_t offset,
    const std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        RecordWriteFailure(GetLastError());
        return false;
    }

    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    bool ok = SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
    if (!ok) {
        RecordWriteFailure(GetLastError());
    }
    if (ok && !bytes.empty()) {
        DWORD written = 0;
        const BOOL write_ok = WriteFile(
            file,
            bytes.data(),
            static_cast<DWORD>(bytes.size()),
            &written,
            nullptr);
        if (write_ok == FALSE) {
            RecordWriteFailure(GetLastError());
            ok = false;
        } else {
            ok = written == static_cast<DWORD>(bytes.size());
        }
    }
    if (ok) {
        ok = FlushFileBuffers(file) != FALSE;
        if (!ok) {
            RecordWriteFailure(GetLastError());
        }
    }
    CloseHandle(file);
    return ok;
}

FileState StateAtPath(const fs::path& path, const PatchSpec& spec) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return FileState::Missing;
    }

    std::vector<std::uint8_t> data;
    if (!ReadFileBytes(path, data)) {
        return FileState::ReadError;
    }

    const auto original = OriginalBytes(spec);
    const auto patched = PatchedBytes(spec);
    if (patched.empty()) {
        return FileState::Unexpected;
    }

    const std::uint64_t end = spec.offset + original.size();
    if (end > data.size()) {
        return FileState::TooSmall;
    }

    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(spec.offset);
    if (std::equal(original.begin(), original.end(), begin)) {
        return FileState::Original;
    }
    if (std::equal(patched.begin(), patched.end(), begin)) {
        return FileState::Patched;
    }
    return FileState::Unexpected;
}

fs::path LocalizationDir(const fs::path& game_root) {
    return game_root / L"Common" / L"Localisation";
}

fs::path LocalizationBackupDir(const fs::path& game_root) {
    return LocalizationDir(game_root) / L"BACKUP";
}


bool LooksLikeGameRoot(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path / kGameExeName, ec);
}

std::optional<fs::path> ResolveGameRoot(const fs::path& selected) {
    std::error_code ec;
    fs::path candidate = selected;
    if (fs::is_regular_file(candidate, ec)) {
        candidate = candidate.parent_path();
    }

    for (int depth = 0; depth < 4 && !candidate.empty(); ++depth) {
        if (LooksLikeGameRoot(candidate)) {
            return candidate;
        }
        const fs::path parent = candidate.parent_path();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    return std::nullopt;
}

std::optional<std::wstring> ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring data(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, data.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    while (!data.empty() && data.back() == L'\0') {
        data.pop_back();
    }
    return data.empty() ? std::nullopt : std::optional<std::wstring>(data);
}

std::wstring UnescapeVdfPath(std::wstring value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size() && value[i + 1] == L'\\') {
            result.push_back(L'\\');
            ++i;
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::vector<fs::path> ParseSteamLibraryFolders(const fs::path& steam_root) {
    std::vector<fs::path> libraries;
    libraries.push_back(steam_root);

    const fs::path vdf = steam_root / L"steamapps" / L"libraryfolders.vdf";
    std::ifstream input(vdf, std::ios::binary);
    if (!input) {
        return libraries;
    }

    std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::wstring text = Utf8ToWide(raw);
    std::size_t pos = 0;
    while ((pos = text.find(L"\"path\"", pos)) != std::wstring::npos) {
        pos += 6;
        const std::size_t value_begin = text.find(L'"', pos);
        if (value_begin == std::wstring::npos) {
            break;
        }
        const std::size_t value_end = text.find(L'"', value_begin + 1);
        if (value_end == std::wstring::npos) {
            break;
        }
        const std::wstring value = UnescapeVdfPath(text.substr(value_begin + 1, value_end - value_begin - 1));
        if (!value.empty()) {
            libraries.emplace_back(value);
        }
        pos = value_end + 1;
    }
    return libraries;
}

std::optional<fs::path> AutoDetectGameRoot() {
    std::vector<fs::path> direct_candidates;
    try {
        direct_candidates.push_back(fl4tout::ExecutablePath().parent_path());
    } catch (...) {
    }
    std::error_code ec;
    direct_candidates.push_back(fs::current_path(ec));

    for (const auto& candidate : direct_candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (const auto root = ResolveGameRoot(candidate)) {
            LogInfo(std::wstring(L"Found game near installer: ") + root->wstring());
            return root;
        }
    }

    std::vector<fs::path> steam_roots;
    if (const auto path = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
        steam_roots.emplace_back(*path);
    }
    if (const auto path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath")) {
        steam_roots.emplace_back(*path);
    }

    wchar_t program_files[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files, MAX_PATH) > 0) {
        steam_roots.emplace_back(fs::path(program_files) / L"Steam");
    }

    std::set<std::wstring> seen;
    for (const auto& steam_root : steam_roots) {
        if (steam_root.empty()) {
            continue;
        }
        const std::wstring root_key = steam_root.lexically_normal().wstring();
        if (!seen.insert(root_key).second) {
            continue;
        }

        for (const auto& library : ParseSteamLibraryFolders(steam_root)) {
            const fs::path candidate = library / L"steamapps" / L"common" / kSteamGameDirName;
            if (LooksLikeGameRoot(candidate)) {
                LogInfo(std::wstring(L"Found Steam game folder: ") + candidate.wstring());
                return candidate;
            }
        }
    }

    LogWarn(std::wstring(kGameShortName) + L" was not found automatically.");
    return std::nullopt;
}

std::optional<fs::path> ChooseGameFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    const HRESULT create_result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(create_result)) {
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose the FlatOut 4 VR game folder");

    const HRESULT show_result = dialog->Show(owner);
    if (FAILED(show_result)) {
        dialog->Release();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }

    PWSTR raw_path = nullptr;
    const HRESULT path_result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path);
    std::optional<fs::path> result;
    if (SUCCEEDED(path_result) && raw_path != nullptr) {
        result = fs::path(raw_path);
    }

    CoTaskMemFree(raw_path);
    item->Release();
    dialog->Release();
    return result;
}

int ShowDialog(
    HWND owner,
    const wchar_t* instruction,
    const std::wstring& content,
    const std::vector<TASKDIALOG_BUTTON>& buttons,
    int default_button,
    PCWSTR icon,
    bool command_links = false,
    const wchar_t* footer = nullptr) {
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    if (command_links) {
        config.dwFlags |= TDF_USE_COMMAND_LINKS;
    }
    config.pszWindowTitle = kWindowTitle;
    config.pszMainInstruction = instruction;
    config.pszContent = content.c_str();
    config.cButtons = static_cast<UINT>(buttons.size());
    config.pButtons = buttons.empty() ? nullptr : buttons.data();
    config.nDefaultButton = default_button;
    config.pszMainIcon = icon;
    config.pszFooter = footer;
    if (footer != nullptr) {
        config.pszFooterIcon = TD_INFORMATION_ICON;
    }

    int pressed = kButtonClose;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) {
        MessageBoxW(owner, content.c_str(), kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return kButtonClose;
    }
    return pressed;
}

void ShowError(HWND owner, const std::wstring& instruction, const std::wstring& message) {
    LogError(instruction + L": " + message);
    const std::vector<TASKDIALOG_BUTTON> buttons{{kButtonClose, L"Close"}};
    const std::wstring footer = g_installer_log_path.empty()
        ? std::wstring{}
        : std::wstring(L"Install log: ") + g_installer_log_path.wstring();
    ShowDialog(
        owner,
        instruction.c_str(),
        message,
        buttons,
        kButtonClose,
        TD_ERROR_ICON,
        false,
        footer.empty() ? nullptr : footer.c_str());
}

GameValidation ValidateGameBuild(const fs::path& game_root) {
    GameValidation result;
    const fs::path exe = game_root / kGameExeName;
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) {
        result.problem = L"Flatout.exe was not found in the selected folder.";
        LogWarn(result.problem);
        return result;
    }

    result.found = true;
    result.size = fs::file_size(exe, ec);
    if (ec) {
        result.problem = L"Flatout.exe could not be read.";
        LogWarn(result.problem);
        return result;
    }

    {
        std::wostringstream line;
        line << L"Flatout.exe size: " << result.size << L" bytes";
        LogInfo(line.str());
    }

    if (result.size != kExpectedExeSize) {
        result.problem = std::wstring(L"This is not the supported ") + kGameOfficialName + L" v1.87 executable.";
        LogWarn(result.problem);
        return result;
    }

    const auto hash = fl4tout::Sha256File(exe);
    if (!hash) {
        result.problem = L"Flatout.exe could not be hashed.";
        LogWarn(result.problem);
        return result;
    }
    result.sha256 = *hash;
    LogInfo(std::wstring(L"Flatout.exe SHA-256: ") + Utf8ToWide(result.sha256));

    if (result.sha256 != kExpectedExeSha256) {
        result.problem = std::wstring(L"This is not the supported ") + kGameOfficialName + L" v1.87 executable.";
        LogWarn(result.problem);
        return result;
    }

    result.supported = true;
    LogInfo(std::wstring(L"Game build verified: ") + kGameOfficialName + L" v1.87.");
    return result;
}

std::optional<std::wstring> ReadVersionString(const fs::path& path, const wchar_t* key) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return std::nullopt;
    }

    struct Translation {
        WORD language;
        WORD code_page;
    };

    Translation* translations = nullptr;
    UINT translations_size = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&translations), &translations_size) ||
        translations == nullptr || translations_size < sizeof(Translation)) {
        return std::nullopt;
    }

    wchar_t query[128]{};
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%ls",
               translations[0].language, translations[0].code_page, key);

    wchar_t* value = nullptr;
    UINT value_size = 0;
    if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_size) ||
        value == nullptr || value_size == 0) {
        return std::nullopt;
    }
    return std::wstring(value);
}

std::optional<fs::path> InstallerDirectory() {
    try {
        return fl4tout::ExecutablePath().parent_path();
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring SetupFilesProblem() {
    return L"Setup files are incomplete or do not match this release.\n\n"
           L"Re-extract the complete download and keep its folder structure intact, then try again.";
}

bool VerifyPackageFile(
    const fs::path& path,
    const PackageFileSpec& spec,
    std::wstring& detail) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        detail = std::wstring(spec.package_path) + L" is missing.";
        return false;
    }

    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        detail = std::wstring(spec.package_path) + L" could not be read.";
        return false;
    }
    if (size != spec.size) {
        std::wostringstream text;
        text << spec.package_path << L" has the wrong size (" << size
             << L" bytes; expected " << spec.size << L").";
        detail = text.str();
        return false;
    }

    const auto hash = fl4tout::Sha256File(path);
    if (!hash) {
        detail = std::wstring(spec.package_path) + L" could not be hashed.";
        return false;
    }
    if (*hash != spec.sha256) {
        detail = std::wstring(spec.package_path) + L" does not match the SHA-256 for this release.";
        return false;
    }

    if (spec.internal_name != nullptr) {
        const auto product = ReadVersionString(path, L"ProductName");
        const auto internal = ReadVersionString(path, L"InternalName");
        const auto version = ReadVersionString(path, L"ProductVersion");
        if (!product || !internal || !version || *product != kProductName ||
            *internal != spec.internal_name || *version != Utf8ToWide(FL4TOUT_VERSION)) {
            detail = std::wstring(spec.package_path) + L" has unexpected version metadata.";
            return false;
        }
    }

    return true;
}

PackageValidation ValidateSetupPackage() {
    PackageValidation result;
    const auto source_dir = InstallerDirectory();
    if (!source_dir) {
        result.problem = L"The installer could not determine the folder it was started from.";
        LogError(result.problem);
        return result;
    }
    result.source_dir = *source_dir;
    LogInfo(std::wstring(L"Setup files: ") + result.source_dir.wstring());

    const auto verify_spec = [&](const PackageFileSpec& spec) {
        std::wstring detail;
        const fs::path path = result.source_dir / spec.package_path;
        if (!VerifyPackageFile(path, spec, detail)) {
            LogError(std::wstring(L"Setup file verification failed: ") + detail);
            result.problem = SetupFilesProblem();
            return false;
        }

        std::wostringstream line;
        line << L"Verified setup file: " << spec.package_path << L" (" << spec.size
             << L" bytes, SHA-256 " << Utf8ToWide(spec.sha256) << L")";
        LogInfo(line.str());
        return true;
    };

    for (const auto& proxy : kProxyPackages) {
        if (!verify_spec(proxy)) {
            return result;
        }
    }
    if (!verify_spec(kMuteDllPackage) || !verify_spec(kConfigPackage)) {
        return result;
    }

    result.valid = true;
    LogInfo(L"Setup file verification passed.");
    return result;
}

bool IsRecognizedModMetadata(const fs::path& path, const PackageFileSpec& spec) {
    if (spec.internal_name == nullptr) {
        return false;
    }

    const auto product = ReadVersionString(path, L"ProductName");
    const auto company = ReadVersionString(path, L"CompanyName");
    const auto internal = ReadVersionString(path, L"InternalName");
    const auto original = ReadVersionString(path, L"OriginalFilename");
    const auto version = ReadVersionString(path, L"ProductVersion");
    return product && company && internal && original && version &&
           *product == kProductName && *company == kAuthorName &&
           *internal == spec.internal_name && *original == spec.install_name &&
           *version == Utf8ToWide(FL4TOUT_VERSION);
}

bool IsOurComponent(ComponentState state) {
    return state == ComponentState::Current || state == ComponentState::RecognizedMod;
}

ComponentInfo InspectComponent(const fs::path& path, const PackageFileSpec& spec) {
    ComponentInfo info;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return info;
    }

    info.size = fs::file_size(path, ec);
    if (ec) {
        LogWarn(std::wstring(L"Could not read installed component size: ") + path.filename().wstring());
        info.state = ComponentState::Foreign;
        return info;
    }

    const auto hash = fl4tout::Sha256File(path);
    if (!hash) {
        LogWarn(std::wstring(L"Could not hash installed component: ") + path.filename().wstring());
        info.state = ComponentState::Foreign;
        return info;
    }
    info.sha256 = *hash;

    const bool exact_current = info.size == spec.size && info.sha256 == spec.sha256;
    const bool recognized_metadata = IsRecognizedModMetadata(path, spec);
    if (exact_current && recognized_metadata) {
        info.state = ComponentState::Current;
    } else if (recognized_metadata) {
        info.state = ComponentState::RecognizedMod;
    } else {
        info.state = ComponentState::Foreign;
    }

    std::wostringstream line;
    line << L"Installed component: " << path.filename().wstring()
         << L" (" << info.size << L" bytes, SHA-256 " << Utf8ToWide(info.sha256) << L") - ";
    switch (info.state) {
    case ComponentState::Current:
        line << L"current release";
        break;
    case ComponentState::RecognizedMod:
        line << L"recognized FlatOut 4 VR Mute build";
        break;
    case ComponentState::Foreign:
        line << L"foreign/unrecognized file";
        break;
    case ComponentState::Missing:
        break;
    }
    LogInfo(line.str());
    return info;
}

// Installation state comes only from the game-folder files; the installer log is diagnostic.
InstallState InspectInstallState(const fs::path& game_root) {
    InstallState state;

    for (std::size_t index = 0; index < std::size(kProxyPackages); ++index) {
        const auto& proxy = kProxyPackages[index];
        state.proxies[index] = InspectComponent(game_root / proxy.install_name, proxy);
        if (IsOurComponent(state.proxies[index].state) && !state.installed_proxy_index) {
            state.installed_proxy_index = index;
        }
        if (state.proxies[index].state == ComponentState::RecognizedMod) {
            state.has_noncurrent_mod_component = true;
        }
    }

    if (state.installed_proxy_index) {
        state.selected_proxy_index = state.installed_proxy_index;
    } else {
        for (std::size_t index = 0; index < std::size(kProxyPackages); ++index) {
            if (state.proxies[index].state == ComponentState::Missing) {
                state.selected_proxy_index = index;
                break;
            }
        }
    }

    state.payload = InspectComponent(game_root / kPayloadName, kMuteDllPackage);
    if (state.payload.state == ComponentState::RecognizedMod) {
        state.has_noncurrent_mod_component = true;
    }

    std::error_code ec;
    state.config_present = fs::is_regular_file(game_root / kConfigName, ec);
    state.has_any_mod_component =
        state.installed_proxy_index.has_value() || IsOurComponent(state.payload.state);

    if (state.payload.state == ComponentState::Foreign) {
        state.has_foreign_conflict = true;
    } else if (!state.installed_proxy_index && !state.selected_proxy_index) {
        state.has_foreign_conflict = true;
    }
    return state;
}

const PackageFileSpec* SelectedProxyPackage(const InstallState& install) {
    if (!install.selected_proxy_index || *install.selected_proxy_index >= std::size(kProxyPackages)) {
        return nullptr;
    }
    return &kProxyPackages[*install.selected_proxy_index];
}

bool UsesAlternateProxy(const InstallState& install) {
    return !install.installed_proxy_index && install.selected_proxy_index &&
           *install.selected_proxy_index != kPreferredProxyIndex;
}

std::wstring AlternateProxyStatus(const InstallState& install) {
    if (!UsesAlternateProxy(install)) {
        return {};
    }

    const auto* selected = SelectedProxyPackage(install);
    if (selected == nullptr) {
        return {};
    }

    return std::wstring(L"Another mod is already using version.dll. Setup will use ") +
           selected->install_name + L" instead.";
}

ScanResult ScanTargets(const fs::path& game_root) {
    ScanResult result;
    const fs::path localization = LocalizationDir(game_root);

    for (const auto& spec : kPatchSpecs) {
        const fs::path path = localization / spec.filename;
        const FileState state = StateAtPath(path, spec);
        if (state == FileState::Missing) {
            continue;
        }

        result.targets.push_back({path, &spec});
        if (state == FileState::Original) {
            ++result.original_count;
        } else if (state == FileState::Patched) {
            ++result.patched_count;
        } else {
            result.problem_files.emplace_back(spec.filename);
        }
    }

    std::wostringstream line;
    line << L"Localization files: " << result.targets.size()
         << L" found, " << result.original_count << L" original, "
         << result.patched_count << L" patched, " << result.problem_files.size() << L" unexpected.";
    LogInfo(line.str());
    return result;
}


bool EnsureLocalizationBackups(
    const fs::path& game_root,
    const std::vector<Target>& targets,
    std::wstring& problem) {
    const fs::path backup_dir = LocalizationBackupDir(game_root);
    std::error_code ec;
    fs::create_directories(backup_dir, ec);
    if (ec) {
        RecordWriteFailure(ec);
    }
    if (ec || !fs::is_directory(backup_dir, ec)) {
        if (ec) {
            RecordWriteFailure(ec);
        }
        problem = L"Could not create the localization BACKUP folder.";
        LogError(problem + L" Path: " + backup_dir.wstring());
        return false;
    }

    for (const auto& target : targets) {
        const fs::path backup = backup_dir / target.path.filename();
        ec.clear();
        const bool backup_exists = fs::exists(backup, ec);
        if (ec) {
            problem = std::wstring(L"Could not inspect backup for ") +
                      target.path.filename().wstring() + L".";
            LogError(problem);
            return false;
        }
        if (backup_exists) {
            ec.clear();
            if (!fs::is_regular_file(backup, ec) || ec ||
                StateAtPath(backup, *target.spec) != FileState::Original) {
                problem = std::wstring(L"Existing backup is not a valid original copy: ") +
                          target.path.filename().wstring() + L".";
                LogError(problem);
                return false;
            }
            LogInfo(std::wstring(L"Backup already present: ") + target.path.filename().wstring());
            continue;
        }

        const FileState live_state = StateAtPath(target.path, *target.spec);
        if (live_state != FileState::Original) {
            problem = std::wstring(L"Original backup is missing for ") +
                      target.path.filename().wstring() +
                      L". Restore the original game file before repairing the mod.";
            LogError(problem);
            return false;
        }

        if (!CopyFileW(target.path.c_str(), backup.c_str(), TRUE)) {
            RecordWriteFailure(GetLastError());
            problem = std::wstring(L"Could not back up ") + target.path.filename().wstring() + L".";
            LogError(problem);
            return false;
        }

        if (StateAtPath(backup, *target.spec) != FileState::Original) {
            DeleteFileW(backup.c_str());
            problem = std::wstring(L"Could not verify backup for ") +
                      target.path.filename().wstring() + L".";
            LogError(problem);
            return false;
        }
        LogInfo(std::wstring(L"Backed up ") + target.path.filename().wstring());
    }

    LogInfo(std::wstring(L"Localization backups ready: ") + backup_dir.wstring());
    return true;
}


bool RestorePatchedTargets(const std::vector<const Target*>& changed_targets) {
    bool restored_all = true;
    for (auto it = changed_targets.rbegin(); it != changed_targets.rend(); ++it) {
        const Target& target = **it;
        const auto original = OriginalBytes(*target.spec);
        if (!WriteBytesAtOffset(target.path, target.spec->offset, original) ||
            StateAtPath(target.path, *target.spec) != FileState::Original) {
            restored_all = false;
        }
    }
    return restored_all;
}

bool PatchLocalization(
    const std::vector<Target>& targets,
    std::size_t& changed,
    std::vector<const Target*>& changed_targets,
    std::wstring& problem) {
    changed = 0;
    changed_targets.clear();

    for (const auto& target : targets) {
        const FileState state = StateAtPath(target.path, *target.spec);
        if (state == FileState::Patched) {
            continue;
        }
        if (state != FileState::Original) {
            problem = std::wstring(L"Unexpected localization data in ") + target.path.filename().wstring() + L".";
            RestorePatchedTargets(changed_targets);
            return false;
        }

        const auto patched = PatchedBytes(*target.spec);
        if (!WriteBytesAtOffset(target.path, target.spec->offset, patched)) {
            const auto original = OriginalBytes(*target.spec);
            WriteBytesAtOffset(target.path, target.spec->offset, original);
            problem = std::wstring(L"Could not update ") + target.path.filename().wstring() + L".";
            RestorePatchedTargets(changed_targets);
            return false;
        }
        changed_targets.push_back(&target);
        if (StateAtPath(target.path, *target.spec) != FileState::Patched) {
            problem = std::wstring(L"Could not verify ") + target.path.filename().wstring() + L".";
            RestorePatchedTargets(changed_targets);
            return false;
        }
        ++changed;
        LogInfo(std::wstring(L"Patched ") + target.path.filename().wstring());
    }
    return true;
}

bool RestoreLocalizationInPlace(
    const std::vector<Target>& targets,
    std::size_t& changed,
    std::vector<std::wstring>& warnings) {
    changed = 0;
    bool clean = true;

    for (const auto& target : targets) {
        const FileState state = StateAtPath(target.path, *target.spec);
        if (state == FileState::Original) {
            continue;
        }
        if (state != FileState::Patched) {
            warnings.emplace_back(target.path.filename().wstring() + L" was left unchanged because its text no longer matches the installer patch.");
            clean = false;
            continue;
        }

        const auto original = OriginalBytes(*target.spec);
        if (!WriteBytesAtOffset(target.path, target.spec->offset, original) ||
            StateAtPath(target.path, *target.spec) != FileState::Original) {
            warnings.emplace_back(target.path.filename().wstring() + L" could not be restored.");
            clean = false;
            continue;
        }

        ++changed;
        LogInfo(std::wstring(L"Restored ") + target.path.filename().wstring());
    }
    return clean;
}

bool CopyPackageFile(
    const fs::path& source_dir,
    const PackageFileSpec& spec,
    const fs::path& destination,
    std::wstring& problem) {
    const fs::path source = source_dir / spec.package_path;
    std::wstring detail;
    if (!VerifyPackageFile(source, spec, detail)) {
        LogError(std::wstring(L"Setup file verification failed before copy: ") + detail);
        problem = SetupFilesProblem();
        return false;
    }

    std::error_code ec;
    if (fs::equivalent(source, destination, ec) && !ec) {
        LogInfo(std::wstring(L"Setup file already in game folder: ") + spec.install_name);
        return true;
    }

    if (!CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
        RecordWriteFailure(GetLastError());
        problem = std::wstring(L"Could not install ") + destination.filename().wstring() + L".";
        LogError(problem);
        return false;
    }

    detail.clear();
    if (!VerifyPackageFile(destination, spec, detail)) {
        problem = std::wstring(L"Installed file verification failed for ") + destination.filename().wstring() + L".";
        LogError(problem + L" " + detail);
        return false;
    }
    return true;
}

bool InstallPayloadFiles(
    const fs::path& game_root,
    const PackageValidation& package,
    const InstallState& install,
    std::wstring& problem) {
    if (!package.valid) {
        problem = package.problem.empty() ? SetupFilesProblem() : package.problem;
        return false;
    }

    const PackageFileSpec* proxy_package = SelectedProxyPackage(install);
    if (proxy_package == nullptr) {
        problem = L"No supported proxy loader filename is available in the game folder.";
        return false;
    }

    const fs::path payload = game_root / kPayloadName;
    const fs::path proxy = game_root / proxy_package->install_name;
    const fs::path config = game_root / kConfigName;

    if (!CopyPackageFile(package.source_dir, kMuteDllPackage, payload, problem)) {
        return false;
    }
    LogInfo(std::wstring(L"Installed ") + kPayloadName);

    if (!CopyPackageFile(package.source_dir, *proxy_package, proxy, problem)) {
        return false;
    }
    LogInfo(std::wstring(L"Installed proxy loader: ") + proxy_package->install_name);

    std::error_code ec;
    if (!fs::exists(config, ec)) {
        if (!CopyPackageFile(package.source_dir, kConfigPackage, config, problem)) {
            return false;
        }
        LogInfo(std::wstring(L"Created ") + kConfigName);
    } else {
        LogInfo(std::wstring(L"Preserved existing ") + kConfigName);
    }
    return true;
}

bool RemoveIfOurs(
    const fs::path& path,
    const PackageFileSpec& spec,
    std::vector<std::wstring>& warnings) {
    const ComponentInfo info = InspectComponent(path, spec);
    if (info.state == ComponentState::Missing) {
        return true;
    }
    if (info.state == ComponentState::Foreign) {
        warnings.emplace_back(path.filename().wstring() + L" was not removed because it does not belong to " + kProductName + L".");
        return false;
    }

    if (!DeleteFileW(path.c_str())) {
        RecordWriteFailure(GetLastError());
        warnings.emplace_back(path.filename().wstring() + L" could not be removed. Close FlatOut 4 VR and try again.");
        return false;
    }
    LogInfo(std::wstring(L"Removed ") + path.filename().wstring());
    return true;
}

bool RemovePlainFile(const fs::path& path, std::vector<std::wstring>& warnings) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }
    if (!fs::is_regular_file(path, ec)) {
        warnings.emplace_back(path.filename().wstring() + L" was not removed because it is not a regular file.");
        return false;
    }
    if (!DeleteFileW(path.c_str())) {
        RecordWriteFailure(GetLastError());
        warnings.emplace_back(path.filename().wstring() + L" could not be removed.");
        return false;
    }
    LogInfo(std::wstring(L"Removed ") + path.filename().wstring());
    return true;
}

bool ValidateInstallPreflight(
    const GameValidation& game,
    const PackageValidation& package,
    const InstallState& install,
    const ScanResult& scan,
    std::wstring& problem) {
    if (!package.valid) {
        problem = package.problem.empty() ? SetupFilesProblem() : package.problem;
        return false;
    }
    if (!game.supported) {
        problem = game.problem.empty() ? std::wstring(kGameShortName) + L" v1.87 is required." : game.problem;
        return false;
    }
    if (install.has_noncurrent_mod_component) {
        problem = std::wstring(L"A different ") + kProductName + L" " +
                  Utf8ToWide(FL4TOUT_VERSION) + L" build is already installed.\n\n" +
                  L"Uninstall it first, then run this Setup again for a clean install.";
        return false;
    }
    if (install.payload.state == ComponentState::Foreign) {
        problem = std::wstring(L"A different ") + kPayloadName +
                  L" already exists in the game folder.\n\nThe installer will not overwrite another mod's file.";
        return false;
    }
    if (!install.selected_proxy_index) {
        problem = L"version.dll and winhttp.dll are already in use in the game folder.\n\n"
                  L"Setup needs one of these loader filenames and will not overwrite another mod's file.";
        return false;
    }
    if (*install.selected_proxy_index >= std::size(kProxyPackages) ||
        install.proxies[*install.selected_proxy_index].state == ComponentState::Foreign) {
        problem = L"The selected proxy-loader filename is no longer available.\n\n"
                  L"Return to the game-folder page and try again.";
        return false;
    }
    if (scan.targets.empty()) {
        problem = L"No supported localization files were found.";
        return false;
    }
    if (!scan.problem_files.empty()) {
        std::wostringstream text;
        text << L"Some localization files do not match " << kGameShortName << L" v1.87:\n\n";
        for (const auto& file : scan.problem_files) {
            text << L"• " << file << L"\n";
        }
        text << L"\nVerify the game files in Steam, then try again.";
        problem = text.str();
        return false;
    }
    return true;
}

struct InstallerSettings {
    bool always_mute_self = false;
    bool always_mute_others = false;
};

InstallerSettings LoadInstallerSettings(const fs::path& game_root) {
    const fs::path path = game_root / kConfigName;
    InstallerSettings settings;
    settings.always_mute_self = GetPrivateProfileIntW(L"General", L"AlwaysMuteSelf", 0, path.c_str()) != 0;
    settings.always_mute_others = GetPrivateProfileIntW(L"General", L"AlwaysMuteOthers", 0, path.c_str()) != 0;
    return settings;
}

bool SaveInstallerSettingsDirect(
    const fs::path& game_root,
    const InstallerSettings& settings,
    std::wstring& problem) {
    const fs::path path = game_root / kConfigName;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        const auto source_dir = InstallerDirectory();
        if (!source_dir) {
            problem = L"The installer could not determine the folder it was started from.";
            return false;
        }
        if (!CopyPackageFile(*source_dir, kConfigPackage, path, problem)) {
            return false;
        }
    }

    const auto write_bool = [&](const wchar_t* key, bool value) {
        SetLastError(ERROR_SUCCESS);
        const BOOL written = WritePrivateProfileStringW(
            L"General", key, value ? L"1" : L"0", path.c_str());
        if (written == FALSE) {
            RecordWriteFailure(GetLastError());
            return false;
        }
        return true;
    };

    if (!write_bool(L"AlwaysMuteSelf", settings.always_mute_self) ||
        !write_bool(L"AlwaysMuteOthers", settings.always_mute_others)) {
        problem = L"The settings file could not be updated.";
        return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());

    const InstallerSettings verify = LoadInstallerSettings(game_root);
    if (verify.always_mute_self != settings.always_mute_self ||
        verify.always_mute_others != settings.always_mute_others) {
        problem = L"The settings file was written but could not be verified.";
        return false;
    }

    std::wostringstream line;
    line << L"Settings saved: AlwaysMuteSelf=" << (settings.always_mute_self ? 1 : 0)
         << L", AlwaysMuteOthers=" << (settings.always_mute_others ? 1 : 0);
    LogInfo(line.str());
    return true;
}

ActionResult SaveInstallerSettings(
    HWND owner,
    const fs::path& game_root,
    const InstallerSettings& settings) {
    ResetActionWriteState();
    std::wstring problem;
    if (!SaveInstallerSettingsDirect(game_root, settings, problem)) {
        if (ShouldRestartAsAdministrator()) {
            LogWarn(L"Settings write was denied; administrator permission is available as a retry.");
            return ActionResult::RestartAsAdministrator;
        }
        ShowError(owner, L"Settings could not be saved", problem);
        return ActionResult::Retry;
    }
    return ActionResult::Completed;
}

ActionResult RunInstall(
    HWND owner,
    const fs::path& game_root,
    const GameValidation& game,
    const InstallState& install,
    const ScanResult& scan,
    std::size_t& patched_count_out) {
    ResetActionWriteState();
    patched_count_out = 0;
    std::wstring problem;
    const PackageValidation package = ValidateSetupPackage();
    if (!ValidateInstallPreflight(game, package, install, scan, problem)) {
        ShowError(owner, L"Install blocked", problem);
        return ActionResult::Retry;
    }

    LogInfo(std::wstring(L"Install started: ") + game_root.wstring());
    if (!EnsureLocalizationBackups(game_root, scan.targets, problem)) {
        if (ShouldRestartAsAdministrator()) {
            LogWarn(L"Install write was denied while preparing localization backups.");
            return ActionResult::RestartAsAdministrator;
        }
        ShowError(owner, L"Install failed", problem);
        return ActionResult::Retry;
    }

    std::size_t patched_count = 0;
    std::vector<const Target*> changed_targets;
    if (!PatchLocalization(scan.targets, patched_count, changed_targets, problem)) {
        if (ShouldRestartAsAdministrator()) {
            LogWarn(L"Install write was denied while updating localization files.");
            return ActionResult::RestartAsAdministrator;
        }
        ShowError(owner, L"Install failed", problem);
        return ActionResult::Retry;
    }

    if (!InstallPayloadFiles(game_root, package, install, problem)) {
        const bool rolled_back = RestorePatchedTargets(changed_targets);
        if (ShouldRestartAsAdministrator()) {
            LogWarn(rolled_back
                        ? L"Install write was denied; localization changes were rolled back before administrator retry."
                        : L"Install write was denied and some localization rollback work also failed.");
            return ActionResult::RestartAsAdministrator;
        }
        ShowError(
            owner,
            L"Install failed",
            problem + (rolled_back
                           ? L"\n\nLocalization changes from this install were rolled back."
                           : L"\n\nSome localization changes could not be rolled back. See the install log."));
        return ActionResult::Retry;
    }

    patched_count_out = patched_count;
    LogInfo(L"Install completed successfully.");
    return ActionResult::Completed;
}

ActionResult RunUninstall(
    HWND owner,
    const fs::path& game_root,
    const InstallState& install,
    const ScanResult& scan,
    std::vector<std::wstring>& warnings_out) {
    ResetActionWriteState();
    warnings_out.clear();

    LogInfo(std::wstring(L"Uninstall started: ") + game_root.wstring());
    std::size_t restored_count = 0;
    RestoreLocalizationInPlace(scan.targets, restored_count, warnings_out);

    for (std::size_t index = 0; index < std::size(kProxyPackages); ++index) {
        if (IsOurComponent(install.proxies[index].state)) {
            RemoveIfOurs(game_root / kProxyPackages[index].install_name, kProxyPackages[index], warnings_out);
        }
    }
    RemoveIfOurs(game_root / kPayloadName, kMuteDllPackage, warnings_out);
    RemovePlainFile(game_root / kConfigName, warnings_out);
    RemovePlainFile(game_root / kRuntimeLogName, warnings_out);

    if (ShouldRestartAsAdministrator()) {
        LogWarn(L"Uninstall write was denied; administrator permission is available as a retry.");
        return ActionResult::RestartAsAdministrator;
    }

    if (warnings_out.empty()) {
        LogInfo(L"Uninstall completed successfully.");
    } else {
        for (const auto& warning : warnings_out) {
            LogWarn(warning);
        }
        LogWarn(L"Uninstall completed with notes.");
    }
    return ActionResult::Completed;
}

enum class WizardPage {
    Welcome,
    Location,
    Maintenance,
    Ready,
    Settings,
    Finish,
};

enum class MaintenanceChoice {
    Repair,
    Settings,
    Uninstall,
};

constexpr int kIdTitle = 2001;
constexpr int kIdSubtitle = 2002;
constexpr int kIdBody = 2003;
constexpr int kIdCompatLabel = 2004;
constexpr int kIdCompatValue = 2005;
constexpr int kIdDetectedLabel = 2006;
constexpr int kIdDetectedValue = 2007;
constexpr int kIdAuthor = 2008;
constexpr int kIdGithub = 2009;
constexpr int kIdPathLabel = 2010;
constexpr int kIdPathEdit = 2011;
constexpr int kIdBrowse = 2012;
constexpr int kIdStatus = 2013;
constexpr int kIdRepair = 2014;
constexpr int kIdRepairDesc = 2015;
constexpr int kIdSettingsOnly = 2016;
constexpr int kIdSettingsDesc = 2017;
constexpr int kIdUninstall = 2018;
constexpr int kIdUninstallDesc = 2019;
constexpr int kIdSummary = 2020;
constexpr int kIdMuteSelf = 2021;
constexpr int kIdMuteSelfDesc = 2022;
constexpr int kIdMuteOthers = 2023;
constexpr int kIdMuteOthersDesc = 2024;
constexpr int kIdFinishText = 2025;
constexpr int kIdBack = 2101;
constexpr int kIdNext = 2102;
constexpr int kIdCancel = 2103;

struct WizardState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    UINT dpi = 96;
    WizardPage page = WizardPage::Welcome;
    MaintenanceChoice maintenance = MaintenanceChoice::Settings;
    RequestedAction ready_action = RequestedAction::Install;
    std::optional<fs::path> game_root;
    GameValidation game;
    PackageValidation package;
    InstallState install;
    ScanResult scan;
    InstallerSettings settings;
    bool settings_override_pending = false;
    bool settings_after_install = false;
    bool status_good = false;
    bool post_install = false;
    bool post_uninstall = false;
    bool post_settings = false;
    std::size_t patched_count = 0;
    std::vector<std::wstring> uninstall_warnings;

    HFONT font_normal = nullptr;
    HFONT font_small = nullptr;
    HFONT font_bold = nullptr;
    HFONT font_title = nullptr;
    HFONT font_sidebar_title = nullptr;
    HBRUSH white_brush = nullptr;
    HBRUSH bottom_brush = nullptr;

    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND body = nullptr;
    HWND compat_label = nullptr;
    HWND compat_value = nullptr;
    HWND detected_label = nullptr;
    HWND detected_value = nullptr;
    HWND author = nullptr;
    HWND github = nullptr;
    HWND path_label = nullptr;
    HWND path_edit = nullptr;
    HWND browse = nullptr;
    HWND status = nullptr;
    HWND repair = nullptr;
    HWND repair_desc = nullptr;
    HWND settings_only = nullptr;
    HWND settings_desc = nullptr;
    HWND uninstall = nullptr;
    HWND uninstall_desc = nullptr;
    HWND summary = nullptr;
    HWND mute_self = nullptr;
    HWND mute_self_desc = nullptr;
    HWND mute_others = nullptr;
    HWND mute_others_desc = nullptr;
    HWND finish_text = nullptr;
    HWND back = nullptr;
    HWND next = nullptr;
    HWND cancel = nullptr;
};

int Scale(const WizardState& state, int value) {
    return MulDiv(value, static_cast<int>(state.dpi), 96);
}

HFONT MakeFont(const WizardState& state, int points, int weight) {
    return CreateFontW(
        -MulDiv(points, static_cast<int>(state.dpi), 72),
        0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SetControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND AddControl(
    WizardState& state,
    DWORD ex_style,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int id,
    HFONT font = nullptr) {
    HWND control = CreateWindowExW(
        ex_style, class_name, text,
        WS_CHILD | style,
        Scale(state, x), Scale(state, y), Scale(state, width), Scale(state, height),
        state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.instance, nullptr);
    if (control != nullptr) {
        SetControlFont(control, font != nullptr ? font : state.font_normal);
    }
    return control;
}

void ShowControl(HWND control, bool visible) {
    if (control != nullptr) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
}

void SetText(HWND control, const std::wstring& text) {
    if (control != nullptr) {
        SetWindowTextW(control, text.c_str());
    }
}

void HidePageControls(WizardState& state) {
    const HWND controls[] = {
        state.body, state.compat_label, state.compat_value, state.detected_label,
        state.detected_value, state.author, state.github, state.path_label,
        state.path_edit, state.browse, state.status, state.repair,
        state.repair_desc, state.settings_only, state.settings_desc,
        state.uninstall, state.uninstall_desc, state.summary, state.mute_self,
        state.mute_self_desc, state.mute_others, state.mute_others_desc,
        state.finish_text,
    };
    for (HWND control : controls) {
        ShowControl(control, false);
    }
}

void RefreshGameState(WizardState& state) {
    state.game = {};
    state.package = ValidateSetupPackage();
    state.install = {};
    state.scan = {};
    if (!state.game_root) {
        return;
    }

    if (const auto resolved = ResolveGameRoot(*state.game_root)) {
        state.game_root = *resolved;
    } else {
        return;
    }

    LogInfo(std::wstring(L"Selected game folder: ") + state.game_root->wstring());
    state.game = ValidateGameBuild(*state.game_root);
    state.install = InspectInstallState(*state.game_root);
    state.scan = ScanTargets(*state.game_root);
}

std::wstring FriendlyDetectedStatus(const WizardState& state) {
    if (!state.package.valid && !state.install.has_any_mod_component) {
        return L"Setup files are incomplete or do not match this release.";
    }
    if (!state.game_root) {
        return L"Not detected yet — choose the game folder on the next page.";
    }
    if (!state.game.found) {
        return L"Flatout.exe was not found in the selected folder.";
    }
    if (!state.game.supported) {
        if (state.install.has_any_mod_component) {
            return L"A mod installation was found, but this game build is not supported for install or repair.";
        }
        return L"The selected game build is not supported.";
    }
    if (state.install.has_any_mod_component) {
        if (state.install.has_noncurrent_mod_component) {
            std::wstring text = std::wstring(kProductName) + L" is installed (different " +
                                Utf8ToWide(FL4TOUT_VERSION) + L" build) • Uninstall available";
            if (state.install.installed_proxy_index) {
                text += L" • Loader: ";
                text += kProxyPackages[*state.install.installed_proxy_index].install_name;
            }
            return text;
        }
        if (state.install.has_foreign_conflict) {
            return std::wstring(L"A ") + kProductName + L" installation was found with an unrelated conflicting file.";
        }
        std::wstring text = std::wstring(kGameShortName) + L" v1.87 detected • Mod installed (" +
                            Utf8ToWide(FL4TOUT_VERSION) + L")";
        if (state.install.installed_proxy_index) {
            text += L" • Loader: ";
            text += kProxyPackages[*state.install.installed_proxy_index].install_name;
        }
        return text;
    }
    if (state.install.has_foreign_conflict) {
        return state.install.payload.state == ComponentState::Foreign
            ? std::wstring(L"Another mod or unrecognized file is using ") + kPayloadName + L"."
            : L"All supported proxy loader filenames are already in use.";
    }
    if (UsesAlternateProxy(state.install)) {
        return AlternateProxyStatus(state.install);
    }
    return std::wstring(kGameShortName) + L" v1.87 detected and ready.";
}

bool CanContinueFromLocation(const WizardState& state) {
    if (!state.game_root || !state.game.found) {
        return false;
    }
    if (state.install.has_any_mod_component) {
        return true;
    }
    std::wstring problem;
    return ValidateInstallPreflight(state.game, state.package, state.install, state.scan, problem);
}

void UpdateLocationStatus(WizardState& state) {
    std::wstring text;
    bool good = false;
    if (!state.game_root) {
        text = L"Choose the folder containing Flatout.exe. Steam normally uses steamapps\\common\\Project Fox.";
    } else if (!state.game.found) {
        text = L"Flatout.exe was not found in this folder.";
    } else if (state.install.has_any_mod_component && !state.game.supported) {
        text = std::wstring(L"Installed mod detected. Uninstall is available, but install/repair requires ") + kGameShortName + L" v1.87.";
    } else if (!state.game.supported) {
        text = std::wstring(L"Unsupported game build. ") + kGameShortName + L" v1.87 is required.";
    } else if (state.install.has_noncurrent_mod_component) {
        text = std::wstring(kProductName) + L" is installed. This build does not match the current setup files; uninstall is available.";
        good = true;
    } else if (!state.package.valid && state.install.has_any_mod_component) {
        text = L"Installation found. Settings and uninstall are available; repair needs the complete setup files.";
        good = true;
    } else if (!state.package.valid) {
        text = L"Setup files are incomplete or do not match this release. Re-extract the complete download.";
    } else if (state.install.has_foreign_conflict && !state.install.has_any_mod_component) {
        text = state.install.payload.state == ComponentState::Foreign
            ? std::wstring(L"Another mod or unrecognized file is using ") + kPayloadName + L". Setup will not overwrite it."
            : L"version.dll and winhttp.dll are both in use. Setup will not overwrite another mod's loader.";
    } else if (!state.scan.problem_files.empty()) {
        text = L"Localization files do not match v1.87. Verify the game files in Steam.";
    } else if (state.scan.targets.empty()) {
        text = L"No supported localization files were found.";
    } else {
        if (state.install.has_any_mod_component) {
            text = L"Installation found. Continue to repair, change settings, or uninstall.";
        } else if (UsesAlternateProxy(state.install)) {
            text = AlternateProxyStatus(state.install);
        } else {
            text = std::wstring(kGameShortName) + L" v1.87 verified. Ready to continue.";
        }
        good = true;
    }
    state.status_good = good;
    SetText(state.status, text);
    EnableWindow(state.next, CanContinueFromLocation(state));
    InvalidateRect(state.status, nullptr, TRUE);
}

void LoadSettingsControls(WizardState& state) {
    if (state.settings_override_pending) {
        state.settings_override_pending = false;
    } else if (state.game_root) {
        state.settings = LoadInstallerSettings(*state.game_root);
    }
    Button_SetCheck(state.mute_self, state.settings.always_mute_self ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(state.mute_others, state.settings.always_mute_others ? BST_CHECKED : BST_UNCHECKED);
}

InstallerSettings SettingsFromControls(const WizardState& state) {
    InstallerSettings settings;
    settings.always_mute_self = Button_GetCheck(state.mute_self) == BST_CHECKED;
    settings.always_mute_others = Button_GetCheck(state.mute_others) == BST_CHECKED;
    return settings;
}

enum class ResumeAction {
    None,
    Install,
    Uninstall,
    Settings,
};

struct StartupOptions {
    ResumeAction resume = ResumeAction::None;
    std::optional<fs::path> game_root;
    InstallerSettings settings;
    bool settings_after_install = false;
    bool append_log = false;
};

bool ParseBoolArgument(const wchar_t* value) {
    return value != nullptr && wcscmp(value, L"1") == 0;
}

StartupOptions ParseStartupOptions() {
    StartupOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return options;
    }

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--resume-install") {
            options.resume = ResumeAction::Install;
        } else if (arg == L"--resume-uninstall") {
            options.resume = ResumeAction::Uninstall;
        } else if (arg == L"--resume-settings") {
            options.resume = ResumeAction::Settings;
        } else if (arg == L"--append-log") {
            options.append_log = true;
        } else if (arg == L"--game-root" && i + 1 < argc) {
            options.game_root = fs::path(argv[++i]);
        } else if (arg == L"--mute-self" && i + 1 < argc) {
            options.settings.always_mute_self = ParseBoolArgument(argv[++i]);
        } else if (arg == L"--mute-others" && i + 1 < argc) {
            options.settings.always_mute_others = ParseBoolArgument(argv[++i]);
        } else if (arg == L"--settings-after-install" && i + 1 < argc) {
            options.settings_after_install = ParseBoolArgument(argv[++i]);
        }
    }
    LocalFree(argv);

    if (options.resume != ResumeAction::None && !options.game_root) {
        options.resume = ResumeAction::None;
    }
    return options;
}

std::wstring QuoteCommandLineArgument(std::wstring_view value) {
    std::wstring result;
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring BuildElevationParameters(const WizardState& state, ResumeAction action) {
    std::wostringstream parameters;
    parameters << L"--append-log ";
    switch (action) {
    case ResumeAction::Install:
        parameters << L"--resume-install ";
        break;
    case ResumeAction::Uninstall:
        parameters << L"--resume-uninstall ";
        break;
    case ResumeAction::Settings:
        parameters << L"--resume-settings ";
        break;
    case ResumeAction::None:
        break;
    }

    if (state.game_root) {
        parameters << L"--game-root " << QuoteCommandLineArgument(state.game_root->wstring()) << L" ";
    }
    if (action == ResumeAction::Settings) {
        parameters << L"--mute-self " << (state.settings.always_mute_self ? 1 : 0) << L" "
                   << L"--mute-others " << (state.settings.always_mute_others ? 1 : 0) << L" "
                   << L"--settings-after-install " << (state.settings_after_install ? 1 : 0);
    }
    return parameters.str();
}

bool OfferAdministratorRestart(WizardState& state, ResumeAction action) {
    if (IsProcessElevated()) {
        return false;
    }

    const std::vector<TASKDIALOG_BUTTON> buttons{
        {kButtonRestartAsAdministrator, L"Restart as administrator\nContinue this step with the permission Windows requires."},
        {kButtonNotNow, L"Not now"},
    };
    const int pressed = ShowDialog(
        state.window,
        L"Administrator permission is needed",
        L"Windows blocked write access to the selected game folder. Setup can restart with administrator permission and continue from this step.",
        buttons,
        kButtonRestartAsAdministrator,
        TD_SHIELD_ICON,
        true);
    if (pressed != kButtonRestartAsAdministrator) {
        LogInfo(L"Administrator restart was declined.");
        return false;
    }

    fs::path executable;
    try {
        executable = fl4tout::ExecutablePath();
    } catch (...) {
        ShowError(state.window, L"Setup could not restart", L"The installer executable path could not be determined.");
        return false;
    }

    const std::wstring parameters = BuildElevationParameters(state, action);
    const std::wstring working_directory = executable.parent_path().wstring();
    LogInfo(L"Restarting Setup with administrator permission.");
    CloseInstallerLog();

    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.hwnd = state.window;
    execute.lpVerb = L"runas";
    execute.lpFile = executable.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = working_directory.c_str();
    execute.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError();
        OpenInstallerLog(true, false);
        if (error != ERROR_CANCELLED) {
            std::wostringstream message;
            message << L"Windows could not restart Setup with administrator permission. Error " << error << L".";
            ShowError(state.window, L"Setup could not restart", message.str());
        } else {
            LogInfo(L"Windows UAC prompt was cancelled.");
        }
        return false;
    }

    DestroyWindow(state.window);
    return true;
}

void SetNavigation(WizardState& state, bool back_visible, const wchar_t* next_text, bool cancel_visible) {
    ShowControl(state.back, back_visible);
    EnableWindow(state.back, back_visible);
    SetWindowTextW(state.next, next_text);
    ShowControl(state.next, true);
    ShowControl(state.cancel, cancel_visible);
}

void ShowWizardPage(WizardState& state, WizardPage page) {
    state.page = page;
    HidePageControls(state);
    EnableWindow(state.next, TRUE);
    SendMessageW(state.next, BCM_SETSHIELD, 0, FALSE);

    switch (page) {
    case WizardPage::Welcome: {
        SetText(state.title, std::wstring(L"Welcome to ") + kProductName);
        SetText(state.subtitle, std::wstring(L"Voice mute controls for ") + kGameOfficialName + L".");
        SetText(
            state.body,
            std::wstring(kProductName) + L" replaces Show Profile with Mute/Unmute in the " + kGameShortName + L" multiplayer player list and adds simple voice mute controls.\r\n\r\n" +
            L"Setup can install the mod, patch the multiplayer text, save your preferred mute options, and remove everything again later.");
        SetText(state.compat_label, L"COMPATIBLE GAME VERSION");
        SetText(state.compat_value, std::wstring(kGameOfficialName) + L" v" + kSupportedGameVersion + L" • Windows x64");
        SetText(state.detected_label, L"CURRENT STATUS");
        SetText(state.detected_value, FriendlyDetectedStatus(state));
        SetText(state.author, std::wstring(L"Created and maintained by ") + kAuthorName);
        SetText(state.github, L"<a href=\"https://github.com/Dteyn/Flatout4VR-Mute\">github.com/Dteyn/Flatout4VR-Mute</a>");
        ShowControl(state.body, true);
        ShowControl(state.compat_label, true);
        ShowControl(state.compat_value, true);
        ShowControl(state.detected_label, true);
        ShowControl(state.detected_value, true);
        ShowControl(state.author, true);
        ShowControl(state.github, true);
        SetNavigation(state, false, L"Next >", true);
        break;
    }
    case WizardPage::Location: {
        SetText(state.title, std::wstring(L"Choose the ") + kGameShortName + L" folder");
        SetText(state.subtitle, L"Setup checks Steam libraries automatically. You can choose another folder if needed.");
        SetText(state.path_label, L"Game folder:");
        SetText(state.path_edit, state.game_root ? state.game_root->wstring() : L"");
        ShowControl(state.path_label, true);
        ShowControl(state.path_edit, true);
        ShowControl(state.browse, true);
        ShowControl(state.status, true);
        SetNavigation(state, true, L"Next >", true);
        UpdateLocationStatus(state);
        break;
    }
    case WizardPage::Maintenance: {
        SetText(state.title, std::wstring(kProductName) + L" is already installed");
        SetText(state.subtitle, L"Choose what you want Setup to do.");
        SetText(state.repair, L"Repair installation");
        SetText(state.repair_desc,
                L"Reinstall the mod files and refresh the multiplayer text. Your settings are kept.");
        SetText(state.settings_only, L"Change voice mute options");
        SetText(state.settings_desc, L"Change the INI settings with a simple set of checkboxes.");
        SetText(state.uninstall, std::wstring(L"Uninstall ") + kProductName);
        SetText(state.uninstall_desc, L"Remove the mod files and restore the original multiplayer text.");

        const bool repair_ok = state.package.valid && state.game.supported &&
                               !state.install.has_foreign_conflict &&
                               !state.install.has_noncurrent_mod_component &&
                               !state.scan.targets.empty() && state.scan.problem_files.empty();
        EnableWindow(state.repair, repair_ok);
        EnableWindow(state.repair_desc, repair_ok);
        EnableWindow(state.settings_only, state.install.config_present);
        EnableWindow(state.settings_desc, state.install.config_present);

        if (!repair_ok && state.maintenance == MaintenanceChoice::Repair) {
            state.maintenance = state.install.config_present ? MaintenanceChoice::Settings : MaintenanceChoice::Uninstall;
        }
        Button_SetCheck(state.repair, state.maintenance == MaintenanceChoice::Repair ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(state.settings_only, state.maintenance == MaintenanceChoice::Settings ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(state.uninstall, state.maintenance == MaintenanceChoice::Uninstall ? BST_CHECKED : BST_UNCHECKED);

        ShowControl(state.repair, true);
        ShowControl(state.repair_desc, true);
        ShowControl(state.settings_only, true);
        ShowControl(state.settings_desc, true);
        ShowControl(state.uninstall, true);
        ShowControl(state.uninstall_desc, true);
        SetNavigation(state, true, L"Next >", true);
        break;
    }
    case WizardPage::Ready: {
        const bool uninstall = state.ready_action == RequestedAction::Uninstall;
        SetText(state.title, uninstall ? L"Ready to uninstall" : L"Ready to install");
        SetText(
            state.subtitle,
            uninstall
                ? std::wstring(L"Setup is ready to remove ") + kProductName + L"."
                : std::wstring(L"Setup is ready to install ") + kProductName + L" " +
                      Utf8ToWide(FL4TOUT_VERSION) + L".");
        std::wostringstream summary;
        summary << L"Game folder\r\n" << (state.game_root ? state.game_root->wstring() : L"Not selected") << L"\r\n\r\n";
        if (uninstall) {
            summary << L"Setup will:\r\n"
                    << L"  • Remove the " << kProductName << L" DLLs and settings file\r\n"
                    << L"  • Restore the original multiplayer text where it still matches the mod patch\r\n"
                    << L"  • Leave the installer log available for troubleshooting";
        } else {
            summary << L"Compatible game\r\n" << kGameOfficialName << L" v1.87 • Windows x64\r\n\r\n";
            if (const auto* proxy = SelectedProxyPackage(state.install)) {
                summary << L"Proxy loader\r\n" << proxy->install_name;
                if (UsesAlternateProxy(state.install)) {
                    summary << L" (alternate loader)";
                }
                summary << L"\r\n\r\n";
            }
            summary << L"Setup will:\r\n"
                    << L"  • Verify the included DLLs and INI\r\n"
                    << L"  • Copy the mod files\r\n"
                    << L"  • Change Show Profile to Mute/Unmute\r\n"
                    << L"  • Keep your existing settings when repairing";
        }
        SetText(state.summary, summary.str());
        ShowControl(state.summary, true);
        SetNavigation(state, true, uninstall ? L"Uninstall" : L"Install", true);
        break;
    }
    case WizardPage::Settings: {
        SetText(state.title, L"Voice mute options");
        SetText(state.subtitle, std::wstring(L"Choose how the mod should behave when ") + kGameShortName + L" starts.");
        SetText(state.mute_self, L"Start with my microphone muted");
        SetText(state.mute_self_desc, L"Automatically mute yourself when a lobby is created.");
        SetText(state.mute_others, L"Auto-mute other players");
        SetText(state.mute_others_desc, L"Automatically mute other players as they join the lobby.");
        LoadSettingsControls(state);
        ShowControl(state.mute_self, true);
        ShowControl(state.mute_self_desc, true);
        ShowControl(state.mute_others, true);
        ShowControl(state.mute_others_desc, true);
        // "&&" renders as a literal "&"; a single "&" would mark an accelerator key.
        SetNavigation(state, !state.settings_after_install, L"Save && Finish", true);
        break;
    }
    case WizardPage::Finish: {
        SetText(state.title, state.post_uninstall ? L"Uninstall complete" : L"Setup complete");
        SetText(
            state.subtitle,
            state.post_uninstall
                ? std::wstring(kProductName) + L" has been removed."
                : std::wstring(kProductName) + L" " + Utf8ToWide(FL4TOUT_VERSION) + L" is ready to use.");
        std::wostringstream text;
        if (state.post_uninstall) {
            if (state.uninstall_warnings.empty()) {
                text << L"The mod files were removed and the original multiplayer text was restored.";
            } else {
                text << L"The uninstall finished with a few notes:\r\n\r\n";
                for (const auto& warning : state.uninstall_warnings) {
                    text << L"• " << warning << L"\r\n";
                }
            }
        } else if (state.post_settings && !state.post_install) {
            text << L"Your voice mute options were saved. They will be used the next time " << kGameShortName << L" starts.";
        } else {
            text << L"The mod is installed and your voice mute options have been saved.\r\n\r\n"
                 << L"In multiplayer, Show Profile is now Mute/Unmute. Run this installer again at any time to change settings, repair the mod, or uninstall it.";
            if (state.patched_count > 0) {
                text << L"\r\n\r\nLocalization files updated: " << state.patched_count;
            }
        }
        SetText(state.finish_text, text.str());
        ShowControl(state.finish_text, true);
        SetNavigation(state, false, L"Finish", false);
        break;
    }
    }

    InvalidateRect(state.window, nullptr, TRUE);
    UpdateWindow(state.window);
}

void ChooseFolderForWizard(WizardState& state) {
    const auto selected = ChooseGameFolder(state.window);
    if (!selected) {
        return;
    }
    const auto resolved = ResolveGameRoot(*selected);
    state.game_root = resolved ? *resolved : *selected;
    RefreshGameState(state);
    SetText(state.path_edit, state.game_root->wstring());
    UpdateLocationStatus(state);
}

void GoBack(WizardState& state) {
    switch (state.page) {
    case WizardPage::Location:
        ShowWizardPage(state, WizardPage::Welcome);
        break;
    case WizardPage::Maintenance:
        ShowWizardPage(state, WizardPage::Location);
        break;
    case WizardPage::Ready:
        ShowWizardPage(state, state.install.has_any_mod_component ? WizardPage::Maintenance : WizardPage::Location);
        break;
    case WizardPage::Settings:
        if (!state.settings_after_install) {
            ShowWizardPage(state, state.install.has_any_mod_component ? WizardPage::Maintenance : WizardPage::Location);
        }
        break;
    default:
        break;
    }
}

void GoNext(WizardState& state) {
    switch (state.page) {
    case WizardPage::Welcome:
        ShowWizardPage(state, WizardPage::Location);
        return;

    case WizardPage::Location:
        if (!CanContinueFromLocation(state)) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (state.install.has_any_mod_component) {
            state.maintenance = state.install.has_noncurrent_mod_component
                ? MaintenanceChoice::Uninstall
                : (state.install.config_present ? MaintenanceChoice::Settings : MaintenanceChoice::Uninstall);
            ShowWizardPage(state, WizardPage::Maintenance);
        } else {
            state.ready_action = RequestedAction::Install;
            ShowWizardPage(state, WizardPage::Ready);
        }
        return;

    case WizardPage::Maintenance:
        if (Button_GetCheck(state.repair) == BST_CHECKED) {
            state.maintenance = MaintenanceChoice::Repair;
            state.ready_action = RequestedAction::Install;
            ShowWizardPage(state, WizardPage::Ready);
        } else if (Button_GetCheck(state.settings_only) == BST_CHECKED) {
            state.maintenance = MaintenanceChoice::Settings;
            state.settings_after_install = false;
            ShowWizardPage(state, WizardPage::Settings);
        } else {
            state.maintenance = MaintenanceChoice::Uninstall;
            state.ready_action = RequestedAction::Uninstall;
            ShowWizardPage(state, WizardPage::Ready);
        }
        return;

    case WizardPage::Ready: {
        if (!state.game_root) {
            return;
        }
        EnableWindow(state.back, FALSE);
        EnableWindow(state.next, FALSE);
        EnableWindow(state.cancel, FALSE);
        const HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        ActionResult result = ActionResult::Retry;
        if (state.ready_action == RequestedAction::Install) {
            result = RunInstall(
                state.window, *state.game_root, state.game, state.install, state.scan,
                state.patched_count);
        } else {
            result = RunUninstall(
                state.window, *state.game_root, state.install, state.scan,
                state.uninstall_warnings);
        }
        SetCursor(old_cursor);
        EnableWindow(state.cancel, TRUE);
        if (result == ActionResult::RestartAsAdministrator) {
            const ResumeAction resume = state.ready_action == RequestedAction::Install
                ? ResumeAction::Install
                : ResumeAction::Uninstall;
            if (!OfferAdministratorRestart(state, resume)) {
                ShowWizardPage(state, WizardPage::Ready);
            }
            return;
        }
        if (result == ActionResult::Retry) {
            ShowWizardPage(state, WizardPage::Ready);
            return;
        }

        RefreshGameState(state);
        if (state.ready_action == RequestedAction::Install) {
            state.post_install = true;
            state.settings_after_install = true;
            ShowWizardPage(state, WizardPage::Settings);
        } else {
            state.post_uninstall = true;
            ShowWizardPage(state, WizardPage::Finish);
        }
        return;
    }

    case WizardPage::Settings: {
        if (!state.game_root) {
            return;
        }
        state.settings = SettingsFromControls(state);
        EnableWindow(state.next, FALSE);
        const ActionResult result = SaveInstallerSettings(
            state.window, *state.game_root, state.settings);
        if (result == ActionResult::RestartAsAdministrator) {
            if (!OfferAdministratorRestart(state, ResumeAction::Settings)) {
                EnableWindow(state.next, TRUE);
            }
            return;
        }
        if (result == ActionResult::Retry) {
            EnableWindow(state.next, TRUE);
            return;
        }
        state.post_settings = true;
        ShowWizardPage(state, WizardPage::Finish);
        return;
    }

    case WizardPage::Finish:
        DestroyWindow(state.window);
        return;
    }
}

int WizardStep(const WizardState& state) {
    switch (state.page) {
    case WizardPage::Welcome: return 0;
    case WizardPage::Location: return 1;
    case WizardPage::Maintenance:
    case WizardPage::Ready: return 2;
    case WizardPage::Settings: return 3;
    case WizardPage::Finish: return 4;
    }
    return 0;
}

COLOR16 ColorChannel16(BYTE value) {
    return static_cast<COLOR16>(static_cast<unsigned int>(value) << 8);
}

void SetGradientVertex(TRIVERTEX& vertex, int x, int y, BYTE red, BYTE green, BYTE blue) {
    vertex.x = x;
    vertex.y = y;
    vertex.Red = ColorChannel16(red);
    vertex.Green = ColorChannel16(green);
    vertex.Blue = ColorChannel16(blue);
    vertex.Alpha = 0;
}

void DrawHLine(HDC dc, int x1, int x2, int y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen == nullptr) {
        return;
    }

    HGDIOBJ previous = SelectObject(dc, pen);
    MoveToEx(dc, x1, y, nullptr);
    LineTo(dc, x2, y);
    SelectObject(dc, previous);
    DeleteObject(pen);
}

void PaintWizard(HWND window, WizardState& state) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);

    FillRect(dc, &client, state.white_brush);

    const int sidebar_width = Scale(state, kSidebarWidth);
    const int bottom_height = Scale(state, kBottomBarHeight);
    RECT bottom{sidebar_width, client.bottom - bottom_height, client.right, client.bottom};
    FillRect(dc, &bottom, state.bottom_brush);

    // Three-stop vertical gradient: amber -> gold -> pale gold.
    TRIVERTEX vertices[4]{};
    const int gradient_mid_y = client.bottom * 3 / 5;
    SetGradientVertex(vertices[0], 0, 0, 0xB4, 0x7B, 0x0C);
    SetGradientVertex(vertices[1], sidebar_width, gradient_mid_y, 0xD2, 0x8F, 0x0E);
    SetGradientVertex(vertices[2], 0, gradient_mid_y, 0xD2, 0x8F, 0x0E);
    SetGradientVertex(vertices[3], sidebar_width, client.bottom, 0xF0, 0xAE, 0x13);
    GRADIENT_RECT gradients[2]{{0, 1}, {2, 3}};
    GradientFill(dc, vertices, 4, gradients, 2, GRADIENT_FILL_RECT_V);

    DrawHLine(dc, sidebar_width, client.right, client.bottom - bottom_height, kBottomRuleColor);

    if (state.page != WizardPage::Welcome && state.page != WizardPage::Finish) {
        DrawHLine(
            dc,
            Scale(state, kContentLeft),
            Scale(state, kContentRight),
            Scale(state, kHeaderRuleY),
            kHeaderRuleColor);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kSidebarTextColor);
    SelectObject(dc, state.font_sidebar_title);
    RECT brand{
        Scale(state, 26), Scale(state, kBrandY),
        sidebar_width - Scale(state, 18), Scale(state, kBrandY + kBrandH)};
    DrawTextW(dc, L"FLATOUT 4\nVR MUTE", -1, &brand, DT_LEFT | DT_TOP | DT_NOPREFIX);

    SelectObject(dc, state.font_small);
    SetTextColor(dc, kSidebarSubtleTextColor);
    RECT version{
        Scale(state, 26), Scale(state, kVersionY),
        sidebar_width - Scale(state, 18), Scale(state, kVersionY + kVersionH)};
    std::wstring version_text = std::wstring(L"Version ") + Utf8ToWide(FL4TOUT_VERSION);
    DrawTextW(dc, version_text.c_str(), -1, &version, DT_LEFT | DT_TOP | DT_NOPREFIX);

    DrawHLine(
        dc,
        Scale(state, 26),
        sidebar_width - Scale(state, 18),
        Scale(state, kSidebarRuleY),
        kSidebarRuleColor);

    const wchar_t* steps[] = {L"Welcome", L"Game folder", L"Install", L"Options", L"Finish"};
    const int active = WizardStep(state);
    for (int index = 0; index < 5; ++index) {
        const int y = kStepsStartY + index * kStepPitch;
        HBRUSH dot_brush = CreateSolidBrush(index == active ? kSidebarTextColor : kSidebarInactiveDotColor);
        HGDIOBJ previous = SelectObject(dc, dot_brush);
        Ellipse(dc, Scale(state, 28), Scale(state, y + 3), Scale(state, 38), Scale(state, y + 13));
        SelectObject(dc, previous);
        DeleteObject(dot_brush);

        SelectObject(dc, index == active ? state.font_bold : state.font_normal);
        SetTextColor(dc, index == active ? kSidebarTextColor : kSidebarInactiveStepColor);
        RECT step_rect{Scale(state, 48), Scale(state, y), sidebar_width - Scale(state, 12), Scale(state, y + 22)};
        DrawTextW(dc, steps[index], -1, &step_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, state.font_small);
    SetTextColor(dc, kSidebarSubtleTextColor);
    RECT tagline{Scale(state, 26), client.bottom - Scale(state, 58), sidebar_width - Scale(state, 18), client.bottom - Scale(state, 18)};
    DrawTextW(dc, L"Simple voice controls\nfor FlatOut 4 VR multiplayer", -1, &tagline, DT_LEFT | DT_BOTTOM | DT_NOPREFIX);

    EndPaint(window, &paint);
}

void CreateWizardControls(WizardState& state) {
    state.font_normal = MakeFont(state, 9, FW_NORMAL);
    state.font_small = MakeFont(state, 8, FW_NORMAL);
    state.font_bold = MakeFont(state, 10, FW_SEMIBOLD);
    state.font_title = MakeFont(state, 18, FW_SEMIBOLD);
    state.font_sidebar_title = MakeFont(state, 18, FW_BOLD);
    state.white_brush = CreateSolidBrush(RGB(255, 255, 255));
    state.bottom_brush = CreateSolidBrush(RGB(246, 247, 249));

    state.title = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kTitleY, kContentWidth, kTitleH, kIdTitle, state.font_title);
    state.subtitle = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kSubtitleY, kContentWidth, kSubtitleH, kIdSubtitle, state.font_normal);
    ShowControl(state.title, true);
    ShowControl(state.subtitle, true);

    // --- Welcome page ---
    state.body = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kBodyTopY, kContentWidth, kWelcomeBodyH, kIdBody, state.font_normal);
    state.compat_label = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kWelcomeCompatLabelY, kContentWidth, 18, kIdCompatLabel, state.font_small);
    state.compat_value = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kWelcomeCompatValueY, kContentWidth, 25, kIdCompatValue, state.font_bold);
    state.detected_label = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kWelcomeDetectedLabelY, kContentWidth, 18, kIdDetectedLabel, state.font_small);
    state.detected_value = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kWelcomeDetectedValueY, kContentWidth, 45, kIdDetectedValue, state.font_normal);
    state.author = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kWelcomeAuthorY, kContentWidth, 20, kIdAuthor, state.font_small);
    state.github = AddControl(
        state, 0, WC_LINK, L"", WS_TABSTOP,
        kContentLeft, kWelcomeGithubY, kContentWidth, 24, kIdGithub, state.font_small);

    // --- Game folder page ---
    state.path_label = AddControl(
        state, 0, L"STATIC", L"Game folder:", SS_LEFT,
        kContentLeft, kBodyTopY, kContentWidth, 20, kIdPathLabel, state.font_bold);
    state.path_edit = AddControl(
        state, WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_LEFT | ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP,
        kContentLeft, kLocationFieldY, kContentWidth - kBrowseW - kFieldGap, kFieldH, kIdPathEdit);
    state.browse = AddControl(
        state, 0, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP,
        kContentRight - kBrowseW, kLocationFieldY, kBrowseW, kFieldH, kIdBrowse);
    state.status = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kLocationStatusY, kContentWidth, 64, kIdStatus, state.font_normal);

    // --- Maintenance page ---
    state.repair = AddControl(
        state, 0, L"BUTTON", L"Repair installation", BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP,
        kContentLeft, OptionRowY(0), kContentWidth, kOptionRowH, kIdRepair, state.font_bold);
    state.repair_desc = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kDescLeft, OptionDescY(0), kDescWidth, kOptionDescH, kIdRepairDesc, state.font_normal);
    state.settings_only = AddControl(
        state, 0, L"BUTTON", L"Change voice mute options", BS_AUTORADIOBUTTON | WS_TABSTOP,
        kContentLeft, OptionRowY(1), kContentWidth, kOptionRowH, kIdSettingsOnly, state.font_bold);
    state.settings_desc = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kDescLeft, OptionDescY(1), kDescWidth, kOptionDescH, kIdSettingsDesc, state.font_normal);
    state.uninstall = AddControl(
        state, 0, L"BUTTON", L"", BS_AUTORADIOBUTTON | WS_TABSTOP,
        kContentLeft, OptionRowY(2), kContentWidth, kOptionRowH, kIdUninstall, state.font_bold);
    state.uninstall_desc = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kDescLeft, OptionDescY(2), kDescWidth, kOptionDescH, kIdUninstallDesc, state.font_normal);

    // --- Ready page ---
    state.summary = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kBodyTopY, kContentWidth, 280, kIdSummary, state.font_normal);

    // --- Settings page ---
    state.mute_self = AddControl(
        state, 0, L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
        kContentLeft, OptionRowY(0), kContentWidth, kOptionRowH, kIdMuteSelf, state.font_bold);
    state.mute_self_desc = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kDescLeft, OptionDescY(0), kDescWidth, kOptionDescH, kIdMuteSelfDesc, state.font_normal);
    state.mute_others = AddControl(
        state, 0, L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
        kContentLeft, OptionRowY(1), kContentWidth, kOptionRowH, kIdMuteOthers, state.font_bold);
    state.mute_others_desc = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kDescLeft, OptionDescY(1), kDescWidth, kOptionDescH, kIdMuteOthersDesc, state.font_normal);

    // --- Finish page ---
    state.finish_text = AddControl(
        state, 0, L"STATIC", L"", SS_LEFT,
        kContentLeft, kBodyTopY, kContentWidth, 238, kIdFinishText, state.font_normal);
    // --- Navigation ---
    state.back = AddControl(
        state, 0, L"BUTTON", L"< Back", BS_PUSHBUTTON | WS_TABSTOP,
        kBackX, kButtonY, kBackW, kButtonH, kIdBack);
    state.next = AddControl(
        state, 0, L"BUTTON", L"Next >", BS_DEFPUSHBUTTON | WS_TABSTOP,
        kNextX, kButtonY, kNextW, kButtonH, kIdNext);
    state.cancel = AddControl(
        state, 0, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP,
        kCancelX, kButtonY, kCancelW, kButtonH, kIdCancel);
}

void DestroyWizardResources(WizardState& state) {
    const HFONT fonts[] = {
        state.font_normal, state.font_small, state.font_bold,
        state.font_title, state.font_sidebar_title,
    };
    for (HFONT font : fonts) {
        if (font != nullptr) {
            DeleteObject(font);
        }
    }
    if (state.white_brush != nullptr) DeleteObject(state.white_brush);
    if (state.bottom_brush != nullptr) DeleteObject(state.bottom_brush);
}

void CenterWindow(HWND window) {
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK WizardWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WizardState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<WizardState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (state == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
    case WM_CREATE:
        CreateWizardControls(*state);
        ShowWizardPage(*state, state->page);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == kIdBrowse && HIWORD(wparam) == BN_CLICKED) {
            ChooseFolderForWizard(*state);
            return 0;
        }
        if (id == kIdBack && HIWORD(wparam) == BN_CLICKED) {
            GoBack(*state);
            return 0;
        }
        if (id == kIdNext && HIWORD(wparam) == BN_CLICKED) {
            GoNext(*state);
            return 0;
        }
        if (id == kIdCancel && HIWORD(wparam) == BN_CLICKED) {
            DestroyWindow(window);
            return 0;
        }
        if (id == kIdRepair && HIWORD(wparam) == BN_CLICKED) {
            state->maintenance = MaintenanceChoice::Repair;
            return 0;
        }
        if (id == kIdSettingsOnly && HIWORD(wparam) == BN_CLICKED) {
            state->maintenance = MaintenanceChoice::Settings;
            return 0;
        }
        if (id == kIdUninstall && HIWORD(wparam) == BN_CLICKED) {
            state->maintenance = MaintenanceChoice::Uninstall;
            return 0;
        }
        break;
    }

    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header != nullptr && header->idFrom == kIdGithub &&
            (header->code == NM_CLICK || header->code == NM_RETURN)) {
            ShellExecuteW(window, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);
        SetBkMode(dc, TRANSPARENT);
        if (control == state->status) {
            SetTextColor(dc, state->status_good ? RGB(20, 112, 70) : RGB(168, 67, 45));
        } else if (control == state->compat_label || control == state->detected_label ||
                   control == state->author || control == state->repair_desc ||
                   control == state->settings_desc || control == state->uninstall_desc ||
                   control == state->mute_self_desc || control == state->mute_others_desc) {
            SetTextColor(dc, RGB(94, 101, 110));
        } else {
            SetTextColor(dc, RGB(38, 42, 47));
        }
        return reinterpret_cast<LRESULT>(state->white_brush);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(state->white_brush);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintWizard(window, *state);
        return 0;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        DestroyWizardResources(*state);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

bool RunWizard(HINSTANCE instance, WizardState& state) {
    state.instance = instance;
    HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
        state.dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WizardWindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_INSTALLER_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_INSTALLER_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    if (window_class.hIcon == nullptr) {
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (window_class.hIconSm == nullptr) {
        window_class.hIconSm = window_class.hIcon;
    }
    window_class.lpszClassName = L"FlatOut4VRMuteInstallerWizard";
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    RECT desired{0, 0, Scale(state, kWizardWidth), Scale(state, kWizardHeight)};
    AdjustWindowRectEx(&desired, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    HWND window = CreateWindowExW(
        0, window_class.lpszClassName, kWindowTitle,
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top,
        nullptr, nullptr, instance, &state);
    if (window == nullptr) {
        return false;
    }

    CenterWindow(window);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    const StartupOptions startup = ParseStartupOptions();
    OpenInstallerLog(startup.append_log);
    LogInfo(std::wstring(L"Installer executable: ") +
            ([&]() {
                try {
                    return fl4tout::ExecutablePath().wstring();
                } catch (...) {
                    return std::wstring(L"unknown");
                }
            })());
    LogInfo(IsProcessElevated()
                ? L"Installer privilege: elevated administrator"
                : L"Installer privilege: standard user (asInvoker)");

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&common_controls);

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool com_initialized = SUCCEEDED(com_result);

    WizardState state;
    if (startup.resume != ResumeAction::None && startup.game_root) {
        state.game_root = startup.game_root;
        RefreshGameState(state);
        switch (startup.resume) {
        case ResumeAction::Install:
            state.page = WizardPage::Ready;
            state.ready_action = RequestedAction::Install;
            break;
        case ResumeAction::Uninstall:
            state.page = WizardPage::Ready;
            state.ready_action = RequestedAction::Uninstall;
            break;
        case ResumeAction::Settings:
            state.page = WizardPage::Settings;
            state.settings = startup.settings;
            state.settings_override_pending = true;
            state.settings_after_install = startup.settings_after_install;
            break;
        case ResumeAction::None:
            break;
        }
        LogInfo(L"Resumed the requested step after administrator restart.");
    } else {
        state.game_root = AutoDetectGameRoot();
        RefreshGameState(state);
    }

    if (!RunWizard(instance, state)) {
        ShowError(nullptr, L"Setup could not start", L"The installer window could not be created.");
    }

    if (com_initialized) {
        CoUninitialize();
    }
    CloseInstallerLog();
    return 0;
}
