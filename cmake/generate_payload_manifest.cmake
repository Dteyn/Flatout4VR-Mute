if(NOT DEFINED OUTPUT_PATH OR NOT DEFINED VERSION_PROXY_PATH OR
   NOT DEFINED WINHTTP_PROXY_PATH OR
   NOT DEFINED MUTE_DLL_PATH OR NOT DEFINED CONFIG_PATH)
    message(FATAL_ERROR "Payload manifest paths were not provided.")
endif()

foreach(path IN ITEMS
        "${VERSION_PROXY_PATH}"
        "${WINHTTP_PROXY_PATH}"
        "${MUTE_DLL_PATH}"
        "${CONFIG_PATH}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Payload manifest input does not exist: ${path}")
    endif()
endforeach()

function(file_identity path prefix)
    file(SIZE "${path}" ${prefix}_SIZE)
    file(SHA256 "${path}" ${prefix}_SHA256)
    string(TOLOWER "${${prefix}_SHA256}" ${prefix}_SHA256)
    set(${prefix}_SIZE "${${prefix}_SIZE}" PARENT_SCOPE)
    set(${prefix}_SHA256 "${${prefix}_SHA256}" PARENT_SCOPE)
endfunction()

file_identity("${VERSION_PROXY_PATH}" VERSION_PROXY)
file_identity("${WINHTTP_PROXY_PATH}" WINHTTP_PROXY)
file_identity("${MUTE_DLL_PATH}" MUTE_DLL)
file_identity("${CONFIG_PATH}" CONFIG)

file(WRITE "${OUTPUT_PATH}" "#pragma once\n\n")
file(APPEND "${OUTPUT_PATH}" "#include <cstdint>\n\n")
file(APPEND "${OUTPUT_PATH}" "namespace fl4tout::installer_payload {\n\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr std::uintmax_t kVersionProxySize = ${VERSION_PROXY_SIZE};\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr char kVersionProxySha256[] = \"${VERSION_PROXY_SHA256}\";\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr std::uintmax_t kWinhttpProxySize = ${WINHTTP_PROXY_SIZE};\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr char kWinhttpProxySha256[] = \"${WINHTTP_PROXY_SHA256}\";\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr std::uintmax_t kMuteDllSize = ${MUTE_DLL_SIZE};\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr char kMuteDllSha256[] = \"${MUTE_DLL_SHA256}\";\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr std::uintmax_t kConfigSize = ${CONFIG_SIZE};\n")
file(APPEND "${OUTPUT_PATH}" "inline constexpr char kConfigSha256[] = \"${CONFIG_SHA256}\";\n\n")
file(APPEND "${OUTPUT_PATH}" "}  // namespace fl4tout::installer_payload\n")
