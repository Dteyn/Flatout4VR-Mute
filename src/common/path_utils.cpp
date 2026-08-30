#include "common/path_utils.h"

#include <stdexcept>
#include <vector>

namespace fl4tout {
namespace {

std::filesystem::path PathForModule(HMODULE module) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        if (copied < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), copied));
        }
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

std::filesystem::path ModulePath(HMODULE module) {
    return PathForModule(module);
}

std::filesystem::path ModuleDirectory(HMODULE module) {
    return PathForModule(module).parent_path();
}

std::filesystem::path ExecutablePath() {
    return PathForModule(nullptr);
}

}  // namespace fl4tout
