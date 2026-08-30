@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

echo FlatOut 4 VR Mute v0.4.0 - MSVC x64 Release Build
echo.

rem This script can be launched from a normal Command Prompt.
rem It discovers an installed MSVC x64 toolchain, CMake, and Ninja.
rem Intermediate files are kept on the local TEMP drive to avoid timestamp
rem problems on mapped/network project drives. CMake regeneration is disabled
rem because this script always performs an explicit clean configure first.

set "VSROOT="
set "VCENV="
set "CMAKE_EXE="
set "NINJA_EXE="
set "BUILDDIR=%TEMP%\Flatout4VR-Mute-v0.4.0-msvc-release"
set "OUTDIR=%CD%\out\Release"

call :find_visual_studio
if not defined VSROOT (
    echo ERROR: No supported Visual Studio C++ installation was found.
    echo Install the Desktop development with C++ workload and a Windows SDK.
    exit /b 1
)

echo Using Visual Studio: %VSROOT%

if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCENV=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
    call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" (
    set "VCENV=%VSROOT%\Common7\Tools\VsDevCmd.bat"
    call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
) else (
    echo ERROR: Found Visual Studio, but no MSVC environment script exists.
    exit /b 1
)
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: Failed to initialize the x64 MSVC environment using:
    echo   %VCENV%
    exit /b 1
)

where cl.exe >nul 2>nul
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: cl.exe is unavailable after initializing Visual Studio.
    echo Verify that the MSVC x64/x86 build tools are installed.
    exit /b 1
)
where link.exe >nul 2>nul
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: link.exe is unavailable after initializing Visual Studio.
    exit /b 1
)
where rc.exe >nul 2>nul
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: Windows SDK tools are unavailable after initializing Visual Studio.
    echo Install a Windows 10 or Windows 11 SDK through Visual Studio Installer.
    exit /b 1
)

call :find_cmake
if not defined CMAKE_EXE (
    echo ERROR: CMake was not found in PATH or in the selected Visual Studio installation.
    exit /b 1
)

call :find_ninja
if not defined NINJA_EXE (
    echo ERROR: Ninja was not found in PATH or in the selected Visual Studio installation.
    exit /b 1
)

for %%I in ("%NINJA_EXE%") do set "PATH=%%~dpI;%PATH%"

echo Using CMake: %CMAKE_EXE%
echo Using Ninja: %NINJA_EXE%
echo Local build directory: %BUILDDIR%
for /f "tokens=*" %%I in ('cl.exe 2^>^&1 ^| findstr /c:"Version"') do echo MSVC: %%I

echo.
echo Configuring clean x64 Release build...
if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
if exist "%BUILDDIR%" (
    echo ERROR: Could not remove the previous local build directory:
    echo   %BUILDDIR%
    echo Close any program using that directory and try again.
    exit /b 1
)

"%CMAKE_EXE%" -S "%CD%" -B "%BUILDDIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE:STRING=Release ^
    -DCMAKE_MAKE_PROGRAM:FILEPATH="%NINJA_EXE%" ^
    -DCMAKE_SUPPRESS_REGENERATION:BOOL=ON
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo.
echo Building...
"%CMAKE_EXE%" --build "%BUILDDIR%" --parallel
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: Compilation failed.
    echo Local build files were retained at:
    echo   %BUILDDIR%
    exit /b 1
)

echo.
echo Staging release package...
if exist "%OUTDIR%" rmdir /s /q "%OUTDIR%"
mkdir "%OUTDIR%" >nul 2>nul

"%CMAKE_EXE%" --install "%BUILDDIR%" --prefix "%OUTDIR%"
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: CMake install/staging failed.
    exit /b 1
)

if not exist "%OUTDIR%\FlatOut4VR-Mute-Installer.exe" (
    echo ERROR: Build completed, but FlatOut4VR-Mute-Installer.exe was not staged.
    exit /b 1
)
if not exist "%OUTDIR%\readme.txt" (
    echo ERROR: Build completed, but readme.txt was not staged.
    exit /b 1
)
if not exist "%OUTDIR%\loaders\version.dll" (
    echo ERROR: Build completed, but loaders\version.dll was not staged.
    exit /b 1
)
if not exist "%OUTDIR%\loaders\winhttp.dll" (
    echo ERROR: Build completed, but loaders\winhttp.dll was not staged.
    exit /b 1
)
if not exist "%OUTDIR%\fl4tout_voip_mute.dll" (
    echo ERROR: Build completed, but fl4tout_voip_mute.dll was not staged.
    exit /b 1
)
if not exist "%OUTDIR%\fl4tout_voip_mute.ini" (
    echo ERROR: Build completed, but fl4tout_voip_mute.ini was not staged.
    exit /b 1
)

rem Also stage the manual-install components for maintainers/users who prefer
rem the Python localization patcher. These are intentionally NOT included in
rem the normal distribution ZIP.
set "MANUALDIR=%CD%\out\Manual"
if exist "%MANUALDIR%" rmdir /s /q "%MANUALDIR%"
mkdir "%MANUALDIR%" >nul 2>nul
mkdir "%MANUALDIR%\loaders" >nul 2>nul
copy /y "%BUILDDIR%\version.dll" "%MANUALDIR%\loaders\version.dll" >nul || goto :manual_stage_error
copy /y "%BUILDDIR%\winhttp.dll" "%MANUALDIR%\loaders\winhttp.dll" >nul || goto :manual_stage_error
copy /y "%BUILDDIR%\fl4tout_voip_mute.dll" "%MANUALDIR%\fl4tout_voip_mute.dll" >nul || goto :manual_stage_error
copy /y "%CD%\config\fl4tout_voip_mute.ini" "%MANUALDIR%\fl4tout_voip_mute.ini" >nul || goto :manual_stage_error
copy /y "%CD%\tools\patch_localization.py" "%MANUALDIR%\patch_localization.py" >nul || goto :manual_stage_error

set "PACKAGE=%CD%\out\FlatOut4VR-Mute-v0.4.0.zip"
if exist "%PACKAGE%" del /q "%PACKAGE%"

echo.
echo Creating distribution ZIP...
powershell.exe -NoProfile -Command ^
    "Compress-Archive -Path '%OUTDIR%\*' -DestinationPath '%PACKAGE%' -Force"
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: Could not create the distribution ZIP.
    exit /b 1
)
if not exist "%PACKAGE%" (
    echo ERROR: Distribution ZIP was not created.
    exit /b 1
)

echo.
echo Release package created successfully:
echo   %PACKAGE%
echo.
echo Package contents:
echo   FlatOut4VR-Mute-Installer.exe
echo   fl4tout_voip_mute.dll
echo   fl4tout_voip_mute.ini
echo   loaders\version.dll
echo   loaders\winhttp.dll
echo   readme.txt
echo.
echo Manual-install files were also staged at:
echo   %MANUALDIR%
echo.
endlocal
exit /b 0

:manual_stage_error
echo ERROR: Could not stage the manual-install files.
exit /b 1

:find_visual_studio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VSROOT set "VSROOT=%%I"
    )
)
if defined VSROOT exit /b 0

for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VSROOT if exist "%ProgramFiles%\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\18\%%E"
)
for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VSROOT if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\%%E"
)
for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VSROOT if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E"
)
exit /b 0

:find_cmake
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do (
    if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
)
if not defined CMAKE_EXE if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
exit /b 0

:find_ninja
for /f "delims=" %%I in ('where ninja.exe 2^>nul') do (
    if not defined NINJA_EXE set "NINJA_EXE=%%I"
)
if not defined NINJA_EXE if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
exit /b 0
