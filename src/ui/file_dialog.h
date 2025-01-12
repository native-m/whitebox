#pragma once

#include <filesystem>
#include <optional>
#include <utility>
#include <nfd.hpp>

namespace wb {
void init_file_dialog();
void shutdown_file_dialog();

std::optional<std::filesystem::path> pick_folder_dialog();

std::optional<std::filesystem::path>
open_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);

std::optional<std::filesystem::path>
save_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);
} // namespace wb