cmake_minimum_required(VERSION 3.21)
project(Flatout4VRMute VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FlatOut 4 VR Mute targets Windows x64 only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "v0.4.0 requires the Microsoft Visual C++ x64 toolchain.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "A 64-bit MSVC toolchain is required.")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(add_version_resource target description internal_name original_filename file_type)
    set(VI_FILE_DESCRIPTION "${description}")
    set(VI_INTERNAL_NAME "${internal_name}")
    set(VI_ORIGINAL_FILENAME "${original_filename}")
    set(VI_FILE_TYPE "${file_type}")
    set(VI_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_info.rc.in"
        "${VI_OUTPUT}"
        @ONLY
    )
    target_sources(${target} PRIVATE "${VI_OUTPUT}")
endfunction()

add_library(fl4tout_voip_mute SHARED
    src/mod/dllmain.cpp
    src/mod/voip_mute.cpp
    src/common/config.cpp
    src/common/log.cpp
    src/common/path_utils.cpp
    src/common/sha256.cpp
)

target_include_directories(fl4tout_voip_mute PRIVATE src)
target_compile_definitions(fl4tout_voip_mute PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    FL4TOUT_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:FL4TOUT_DEBUG_LOGGING>
)
target_link_libraries(fl4tout_voip_mute PRIVATE bcrypt)
set_target_properties(fl4tout_voip_mute PROPERTIES
    OUTPUT_NAME "fl4tout_voip_mute"
    PREFIX ""
)
add_version_resource(
    fl4tout_voip_mute
    "FlatOut 4 VR Mute - Voice Mute Payload"
    "fl4tout_voip_mute"
    "fl4tout_voip_mute.dll"
    "VFT_DLL"
)

function(add_proxy_loader target output_name source description internal_name)
    add_library(${target} SHARED
        ${source}
        src/proxy/proxy_loader.cpp
    )
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    )
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        PREFIX ""
    )
    add_version_resource(
        ${target}
        "${description}"
        "${internal_name}"
        "${output_name}.dll"
        "VFT_DLL"
    )
endfunction()

add_proxy_loader(
    version_proxy
    "version"
    "src/proxy/version_proxy.cpp;src/proxy/version.def"
    "FlatOut 4 VR Mute - Version API Proxy Loader"
    "version"
)

add_proxy_loader(
    winhttp_proxy
    "winhttp"
    "src/proxy/winhttp_proxy.cpp;src/proxy/winhttp.def"
    "FlatOut 4 VR Mute - WinHTTP Proxy Loader"
    "winhttp"
)

foreach(target fl4tout_voip_mute version_proxy winhttp_proxy)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8 /guard:cf)
    target_link_options(${target} PRIVATE /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /CETCOMPAT)
    set_property(TARGET ${target} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endforeach()

# The release installer copies visible payload files from the release folder.
# Proxy loader variants live under loaders\ so version.dll cannot shadow the
# Windows VERSION API used by the installer itself.
set(INSTALLER_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/fl4tout_voip_mute.ini")
set(INSTALLER_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/installer/FlatOut4VRMute.ico")
set(INSTALLER_PAYLOAD_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/installer_payload_manifest.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT "${INSTALLER_PAYLOAD_MANIFEST}"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT_PATH=${INSTALLER_PAYLOAD_MANIFEST}"
        "-DVERSION_PROXY_PATH=$<TARGET_FILE:version_proxy>"
        "-DWINHTTP_PROXY_PATH=$<TARGET_FILE:winhttp_proxy>"
        "-DMUTE_DLL_PATH=$<TARGET_FILE:fl4tout_voip_mute>"
        "-DCONFIG_PATH=${INSTALLER_CONFIG_PATH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_payload_manifest.cmake"
    DEPENDS
        version_proxy
        winhttp_proxy
        fl4tout_voip_mute
        "${INSTALLER_CONFIG_PATH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_payload_manifest.cmake"
    COMMENT "Generating installer payload verification manifest"
    VERBATIM
)
add_custom_target(installer_payload_manifest DEPENDS "${INSTALLER_PAYLOAD_MANIFEST}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installer_resources.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/installer_resources.rc"
    @ONLY
)

add_executable(flatout4vr_mute_installer WIN32
    src/installer/installer.cpp
    src/common/path_utils.cpp
    src/common/sha256.cpp
    "${INSTALLER_PAYLOAD_MANIFEST}"
    "${CMAKE_CURRENT_BINARY_DIR}/installer_resources.rc"
)
add_dependencies(flatout4vr_mute_installer installer_payload_manifest)

target_include_directories(flatout4vr_mute_installer PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(flatout4vr_mute_installer PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    _WIN32_WINNT=0x0601 WINVER=0x0601
    FL4TOUT_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(flatout4vr_mute_installer PRIVATE
    advapi32
    bcrypt
    comctl32
    msimg32
    ole32
    shell32
    uuid
    version
)
target_compile_options(flatout4vr_mute_installer PRIVATE /W4 /permissive- /EHsc /utf-8 /guard:cf)
target_link_options(flatout4vr_mute_installer PRIVATE
    /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /CETCOMPAT
    /MANIFEST:EMBED
    /MANIFESTUAC:NO
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/installer/installer.manifest"
)
set_property(TARGET flatout4vr_mute_installer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
set_target_properties(flatout4vr_mute_installer PROPERTIES
    OUTPUT_NAME "FlatOut4VR-Mute-Installer"
)
add_version_resource(
    flatout4vr_mute_installer
    "FlatOut 4 VR Mute Installer"
    "FlatOut4VR-Mute-Installer"
    "FlatOut4VR-Mute-Installer.exe"
    "VFT_APP"
)

install(TARGETS flatout4vr_mute_installer fl4tout_voip_mute
    RUNTIME DESTINATION .
)
install(TARGETS version_proxy winhttp_proxy
    RUNTIME DESTINATION loaders
)
install(FILES
    config/fl4tout_voip_mute.ini
    readme.txt
    DESTINATION .
)
