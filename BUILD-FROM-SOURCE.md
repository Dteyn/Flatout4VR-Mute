# Building from Source

How to build FlatOut 4 VR Mute yourself, step by step. For how the mod works, [TECHNICAL-SUMMARY.md](TECHNICAL-SUMMARY.md). For install/usage, see [README.md](README.md).

Building requires a Windows 10/11 x64 machine - the mod, proxy loaders, and installer are all native Win32 code.

## 1. Install prerequisites

You need four things. All are free.

**Visual Studio (or Build Tools) with the C++ workload**

Download [Visual Studio Community](https://visualstudio.microsoft.com/downloads/) (or [Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) if you don't want the full IDE). During install, check **Desktop development with C++**. This also installs the Windows 10/11 SDK and the x64 MSVC compiler, which you'll need.

**CMake** (3.21 or newer) and **Ninja**

Easiest via [winget](https://learn.microsoft.com/en-us/windows/package-manager/winget/) from an ordinary command prompt:

```bat
winget install Kitware.CMake
winget install Ninja-build.Ninja
```

Or download them directly: [cmake.org/download](https://cmake.org/download/) and [github.com/ninja-build/ninja/releases](https://github.com/ninja-build/ninja/releases). Either way, make sure they end up on your PATH - reopen your terminal after installing so it picks up the change.

**Git** (to get the source)

[git-scm.com/downloads](https://git-scm.com/downloads), or `winget install Git.Git`.

## 2. Get the source

```bat
git clone https://github.com/Dteyn/Flatout4VR-Mute.git
cd Flatout4VR-Mute
```

## 3. Build

```bat
build-and-package.bat
```

Run this from a normal command prompt - it finds your Visual Studio install and sets up the x64 build environment itself, so you don't need to open a special developer prompt first.

The script builds the payload DLL and both proxy loaders, generates the installer's file-verification manifest from those exact outputs, builds the installer, and packages everything into a ZIP under `out\`, for example:

```text
out\FlatOut4VR-Mute-v0.4.0.zip
```

It also stages a separate `out\Manual` folder with the manual-install files (payload DLL, INI, `version.dll`, `patch_localization.py`).

This is the only build script you need for a normal release build - no separate steps to build the installer or loaders individually.

## 4. Try it

Extract the ZIP somewhere and run the installer exe, or manually copy the freshly built files to your FlatOut 4 VR folder.

## Faster iteration (development builds)

For active development, rebuilding the whole package each time is slow. You can drive CMake directly instead:

1. Open **x64 Native Tools Command Prompt for VS 2022** from the Start menu (this sets up the compiler environment - `build-and-package.bat` does this step for you, but a manual CMake invocation needs it done first).
2. From the repo root:

```bat
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

Swap `Debug` for `Release` for an optimized build without going through the full packaging script. Debug builds define `FL4TOUT_DEBUG_LOGGING`, which adds extra detail to `fl4tout_voip_mute.log` (RVAs, hook state, timing, and so on) - useful while working on the runtime.

## Handy batch script

Here is a handy batch script that can copy the newly built files to your FlatOut 4 folder. I used this quite a bit during development for quick iterations.

```bat
@echo off

echo.
echo. Done building, copying files...
echo.

rem copy the new dlls
copy /Y "src\out\Release\*" "C:\Program Files (x86)\Steam\steamapps\common\Project Fox"

rem delete the log between runs
del "C:\Program Files (x86)\Steam\steamapps\common\Project Fox\fl4tout_voip_mute.log"

echo.
echo Done!
echo.
```

## If something goes wrong

- **"Could not find Visual Studio" / build tools not found** - make sure the Desktop development with C++ workload is actually installed (not just Visual Studio itself), then try again from a fresh terminal.
- **"cmake" or "ninja" not recognized** - they're not on PATH, or you installed them before opening this terminal. Close and reopen your command prompt (or sign out/in) after installing.
- **Antivirus flags the built installer or DLLs** - expected for freshly built, unsigned binaries. That's exactly why the source is here to build yourself.
- **A clean rebuild is needed** - delete the `build-debug` folder (or whatever `-B` directory you used) and reconfigure; `build-and-package.bat` always builds clean on its own.
