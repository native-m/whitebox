#pragma once

#include <filesystem>
#include <nfd.hpp>
#include <optional>
#include <utility>

namespace wb {
std::optional<std::filesystem::path> pick_folder_dialog();

std::optional<std::filesystem::path>
open_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);

std::optional<std::filesystem::path>
save_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);
} // namespace wb