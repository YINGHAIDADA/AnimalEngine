#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace animal {

// Paths are UTF-8 on every platform.
std::optional<std::vector<std::byte>> readFile(const std::string& path, std::string* error = nullptr);

} // namespace animal
