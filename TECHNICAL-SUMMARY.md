# Technical Summary

Below is a quick overview of how the FlatOut 4 VR Mute mod works. For install/usage see [README.md](README.md). For building from source, see [BUILD-FROM-SOURCE.md](BUILD-FROM-SOURCE.md).

## How it works

A proxy DLL (`version.dll` or `winhttp.dll`) loads beside `Flatout.exe`, forwards the real Windows DLL's exports, and loads the payload `fl4tout_voip_mute.dll`. 

The payload then:

- validates the game is FlatOut 4: Total Insanity VR version 1.87 (checks hash, PE structure, and expected code/vtable signatures) and exits on mismatch;
- hooks the lobby's `ShowPlayerProfile` vtable slot to repurpose it as the Mute/Unmute action;
- **player mute**: calls the game's own `VoiceChatMutePlayer` / `VoiceChatIsPlayerMuted` API to toggle player mute;
- **self mute**: patches a single byte in `ISteamUser::GetVoice` (`85 C0` -> `FF C0`) so the function exits instead of sending a voice packet.

The payload itself does not change the text in game; to change the in-game text from "Show Profile" to "Mute/Unmute", the game's localization files are edited to replace the text shown in game. The installer .exe does this, or a .py script is also included for manual install.

## Localization Patches

Below are the details on the localization patches, which change the in-game text from "Show Profile" to "Mute/Unmute".

Files are located in your Flatout 4 VR game folder, in the `Common\Localisations\` folder.

For manual patching, use a text editor such as Notepad++ to search and replace all instances of the original text.

***NOTE: Spaces must be added for padding, to keep the original string length!***

| File | Language | Original Text | Replacement Text |
|---|---|---|---|
| `LOCALISATION_CH_S.PLOC` | Chinese (Simplified) | 显示档案 | 切换静音 |
| `LOCALISATION_CH_T.PLOC` | Chinese (Traditional) | 顯示設定檔 | 切換靜音 |
| `LOCALISATION_EN.PLOC` | English | Show profile | Mute/Unmute |
| `LOCALISATION_FR.PLOC` | French | Montrer le profil | Muet/non muet |
| `LOCALISATION_GE.PLOC` | German | Profil anzeigen | Stumm an/aus |
| `LOCALISATION_IT.PLOC` | Italian | Mostra profilo | Muto sì/no |
| `LOCALISATION_JA.PLOC` | Japanese | プロフィール表示 | ミュート切替 |
| `LOCALISATION_KO.PLOC` | Korean | 프로필 표시 | 음소거 전환 |
| `LOCALISATION_POL.PLOC` | Polish | Pokaż profil | Wycisz/Włącz |
| `LOCALISATION_POR_B.PLOC` | Portuguese (Brazil) | Exibir perfil | Mudo: sim/não |
| `LOCALISATION_RU.PLOC` | Russian | Показать профиль | Выкл./вкл. звук |
| `LOCALISATION_SP.PLOC` | Spanish | Mostrar perfil | Silencio sí/no |

## Installer

The installer exe is custom built for this project. Source is included in [src/installer](src/installer).

It performs several actions:

- verifies the local payload before installing
- locates the game folder automatically, or allows user to select it if not found
- verifies the game version before installing
- copies the needed files to the game folder (dll files, ini file)
- creates a backup of the game's localization files
- patches the localization files to change "Show Profile" to "Mute/Unmute" (in all languages)
- presents .ini options to the user for easy configuration

The installer keeps a local log; re-running it allows the user to change settings, or to uninstall the mod.

## Config

The mod keeps options saved in the ini file, which can be adjusted by the installer, or directly edited by the user.

```ini
[General]
AlwaysMuteSelf=0     ; start with your own mic muted
AlwaysMuteOthers=0   ; auto-mute remote players as they join
AllowUnmute=1        ; manual override always available; installer never touches this
```

## Project layout

The project source is separated into several folders:

```text
src/mod/          runtime payload (voip_mute.cpp, dllmain.cpp)
src/proxy/        proxy loaders (proxy_loader.cpp, version_proxy.cpp, winhttp_proxy.cpp)
src/installer/    native Win32 installer (installer.cpp)
src/common/       shared config, logging, paths, SHA-256
tools/            fl4tout_voip_scan.py (compatibility scanner), patch_localization.py (manual install)
```

## Build

```bat
build-and-package.bat
```

Requires MSVC (x64, C++17), a Windows 10/11 SDK, CMake, and Ninja. Builds the payload DLL, both proxy loaders, and the installer, then packages a release ZIP under `out\`. 

See [BUILD-FROM-SOURCE.md](BUILD-FROM-SOURCE.md) for more details on building from source.

## Compatibility Scanner

When a future update is released, the mod will not work with it since the patch offsets are hardcoded and will almost certainly be at different addresses in the new version.

An attempt at future-proofing has been made with the included file [tools/fl4tout_voip_scan.py](tools/fl4tout_voip_scan.py).

This script will scan the new .exe file, if it can find the hooks that the mod requires it will list the offset addreses.

The mod can then be recompiled using those new addresses by editing the constants in [src/mod/voip_mute.cpp](src/mod/voip_mute.cpp), and may then work on the new version.

This is of course not guaranteed; the new version may change or remove functions enough that the mod simply will not function and may need to be rewritten, but at least this option may provide a quick fix in event the update is minor.

