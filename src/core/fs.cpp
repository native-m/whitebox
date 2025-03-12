#include "fs.h"

#ifdef WB_PLATFORM_WINDOWS
#include <ShlObj_core.h>
#include <Shlobj.h>
#include <Windows.h>
#include <shellapi.h>
#endif

namespace wb {

Vector<std::byte> read_file_content(const std::filesystem::path& path) {
  File file;
  Vector<std::byte> bytes;
  if (!file.open(path, IOOpenMode::Read))
    return {};
  if (!file.seek(0, IOSeekMode::End))
    return {};
  uint64_t size = file.position();
  file.seek(0, IOSeekMode::Begin);
  bytes.resize(size);
  if (file.read(bytes.data(), size) < size)
    return {};
  return bytes;
}

std::filesystem::path to_system_preferred_path(const std::filesystem::path& path) {
  return std::filesystem::path(path).make_preferred();
}

std::filesystem::path remove_filename_from_path(const std::filesystem::path& path) {
  return std::filesystem::path(path).remove_filename();
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

std::optional<std::filesystem::path> find_file_recursive(
    const std::filesystem::path& dir,
    const std::filesystem::path& filename) {
  if (std::filesystem::is_directory(dir))
    return {};
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      const std::filesystem::path& current_path = entry.path();
      if (current_path.filename() == filename)
        return current_path;
    }
  }
  return std::optional<std::filesystem::path>();
}

}  // namespace wb