#include "fs.h"
#include <filesystem>

#ifdef WB_PLATFORM_LINUX

namespace wb {
  Vector<DirectoryEntry> enumerate_directory(const std::filesystem::path& path) {
    Vector<DirectoryEntry> entries;

    for (const auto& e: std::filesystem::directory_iterator(path)) {
      size_t size;
      uint32_t type;

      if (e.is_directory()) {
        size = 0;
        type = DirectoryEntry::Directory;
      } else {
        size = e.file_size();
        type = DirectoryEntry::File;
      }
      entries.push_back(DirectoryEntry{
        .name = e.path().filename(),
        .size = size,
        .type = type,
      });
    }

    return entries;
  }
}
#endif