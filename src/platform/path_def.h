#pragma once

#include <array>
#include <filesystem>

namespace wb::path_def {
extern const std::filesystem::path userpath;
extern const std::filesystem::path devpath;
extern const std::filesystem::path wbpath;
extern const std::array<std::filesystem::path, 2> vst3_search_path;
} // namespace wb::path_def