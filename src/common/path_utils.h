#pragma once

#include <windows.h>

#include <filesystem>

namespace fl4tout {

std::filesystem::path ModulePath(HMODULE module);
std::filesystem::path ModuleDirectory(HMODULE module);
std::filesystem::path ExecutablePath();

}  // namespace fl4tout
