#pragma once

#include "common.h"
#include <filesystem>

namespace wb {
std::filesystem::path to_system_preferred_path(const std::filesystem::path& path);
void explore_folder(const std::filesystem::path& path);
void locate_file(const std::filesystem::path& path);
}