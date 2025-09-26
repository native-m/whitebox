#pragma once

#include <bit>
#include <filesystem>
#include <optional>

#include "common.h"
#include "io_types.h"
#include "types.h"
#include "vector.h"

namespace wb {

struct File {
  void* handle_;
  bool open_{};

  File();
  ~File();

  bool open(const std::filesystem::path& path, uint32_t flags);
  bool seek(int64_t offset, IOSeekMode mode);
  uint64_t position() const;
  uint32_t read(void* dest, size_t size);
  uint32_t write(const void* src, size_t size);
  void close();

  inline uint32_t write(const char* src, size_t size) {
    return write((const void*)src, size);
  }

  inline bool is_open() const {
    return open_;
  }
};

struct DirectoryEntry {
  enum {
    Folder,
    File,
    Symlink,
  };

  std::filesystem::path path;
  size_t size;
  uint32_t type;

  inline bool is_folder() const { return type == Folder; }
  inline bool is_file() const { return type == File; }
  inline bool is_symlink() const { return type == Symlink; }
};

consteval uint32_t fourcc(const char ch[5]) {
  if constexpr (std::endian::native == std::endian::little)
    return ch[0] | (ch[1] << 8) | (ch[2] << 16) | (ch[3] << 24);
  return (ch[0] << 24) | (ch[1] << 16) | (ch[2] << 8) | ch[3];
}

Vector<std::byte> read_file_content(File& file);

Vector<std::byte> read_file_content(const std::filesystem::path& path);

std::filesystem::path to_system_preferred_path(const std::filesystem::path& path);

std::filesystem::path remove_filename_from_path(const std::filesystem::path& path);

void explore_folder(const std::filesystem::path& path);

void locate_file(const std::filesystem::path& path);

std::optional<std::filesystem::path> find_file_recursive(
    const std::filesystem::path& dir,
    const std::filesystem::path& filename);

Vector<DirectoryEntry> enumerate_directory(const std::filesystem::path& path);

}  // namespace wb