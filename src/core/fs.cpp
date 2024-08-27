#include "fs.h"

#ifdef WB_PLATFORM_WINDOWS
#include <ShlObj_core.h>
#include <Shlobj.h>
#include <Windows.h>
#include <shellapi.h>
#endif

namespace wb {

std::filesystem::path to_system_preferred_path(const std::filesystem::path& path) {
    return std::filesystem::path(path).make_preferred();
}

void explore_folder(const std::filesystem::path& path) {
    if (!std::filesystem::is_directory(path))
        return;
#ifdef WB_PLATFORM_WINDOWS
    ShellExecute(nullptr, L"explore", path.native().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}
void locate_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path))
        return;
#ifdef WB_PLATFORM_WINDOWS
    // Convert forward slash to backward slash
    auto parent_folder = path.parent_path().make_preferred();
    auto file_path = to_system_preferred_path(path);
    PIDLIST_ABSOLUTE dir_il = ILCreateFromPath(parent_folder.c_str());
    PIDLIST_ABSOLUTE file_il = ILCreateFromPath(file_path.c_str());
    SHOpenFolderAndSelectItems(dir_il, 1, (LPCITEMIDLIST*)&file_il, 0);
    ILFree(dir_il);
    ILFree(file_il);
#endif
}
} // namespace wb