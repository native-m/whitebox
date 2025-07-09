#include "path_def.h"

#include "core/common.h"

namespace wb::path_def {

#ifdef NDEBUG
#if defined(WB_PLATFORM_WINDOWS)
const std::filesystem::path userpath{ std::getenv("USERPROFILE") };
#elif defined(WB_PLATFORM_LINUX)
const std::filesystem::path userpath{ std::getenv("HOME") };
#else
const std::filesystem::path userpath{ std::getenv("HOME") };
#endif
#else
const std::filesystem::path userpath{ std::filesystem::current_path() };
#endif
const std::filesystem::path devpath{ std::filesystem::current_path() };
const std::filesystem::path wbpath{ devpath / ".whitebox" };

const std::array<std::filesystem::path, 2> vst3_search_path{
#if defined(WB_PLATFORM_WINDOWS)
  std::getenv("LOCALAPPDATA") / std::filesystem::path("Programs\\Common\\VST3\\"),
  std::getenv("COMMONPROGRAMFILES") / std::filesystem::path("VST3\\"),
#elif defined(WB_PLATFORM_LINUX)
  "/usr/lib/vst3",
  "/usr/local/lib/vst3",
#endif
};

}  // namespace wb::path_def