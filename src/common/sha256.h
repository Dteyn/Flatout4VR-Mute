#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fl4tout {

std::optional<std::string> Sha256File(const std::filesystem::path& path);

}  // namespace fl4tout
