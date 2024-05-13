#pragma once

#include <optional>
#include <filesystem>
#include <utility>
#include <nfd.hpp>

namespace wb {
std::optional<std::filesystem::path>
open_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);

std::optional<std::filesystem::path>
save_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);
} // namespace wb