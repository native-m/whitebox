#pragma once

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <unordered_set>

#include "core/common.h"
#include "engine/track.h"

namespace wb {

struct FileSize {
  double value;
  const char* unit = "B";

  FileSize() : value(0) {
  }

  FileSize(uint64_t size) {
    if (size >= 0 && size < 1000000) {
      value = (double)size / 1000.0;
      unit = "KB";
    } else if (size >= 1000000 && size < 1000000000) {
      value = (double)size / 1000000.0;
      unit = "MB";
    } else if (size >= 1000000000 && size < 1000000000000) {
      value = (double)size / 1000000000.0;
      unit = "GB";
    } else {
      value = (double)size / 1000000000000.0;
      unit = "TB";
    }
  }
};

struct BrowserItem {
  enum Type {
    Directory,
    File,
  };

  enum FileType {
    Unknown,
    Sample,
    Midi,
  };

  Type type;
  FileType file_type;
  BrowserItem* parent;
  FileSize size;
  std::u8string name;
  bool root_dir;
  bool open;
  std::optional<std::vector<BrowserItem>> dir_items;
  std::optional<std::vector<BrowserItem>> file_items;

  std::filesystem::path get_file_path(const std::filesystem::path& root) const {
    std::filesystem::path ret;
    const BrowserItem* item = this;
    while (item != nullptr) {
      ret = (item != this) ? std::filesystem::path(item->name) / ret : std::filesystem::path(item->name);
      item = item->parent;
    }
    return root.parent_path() / ret;
  }
};

struct BrowserDir {
  std::filesystem::path path;
  BrowserItem item;
};

struct BrowserFilePayload {
  BrowserItem::FileType type;
  double content_length;
  double sample_rate;
  std::filesystem::path path;
};

struct BrowserWindow {
  using DirectorySet = std::unordered_set<std::filesystem::path>;
  using DirectoryRefItem = std::pair<DirectorySet::iterator, BrowserItem>;
  DirectorySet directory_set;
  std::vector<DirectoryRefItem> directories;

  bool open_context_menu = false;
  std::filesystem::path context_menu_path;
  BrowserItem* context_menu_item = nullptr;
  uint32_t selected_root_dir;

  bool is_dragging_item = false;
  BrowserItem* last_dragged_item = nullptr;
  BrowserItem* dragging_item = nullptr;
  BrowserItem* selected_item = nullptr;
  BrowserFilePayload drop_payload;

  BrowserWindow();
  void add_directory(const std::filesystem::path& path);
  void remove_directory(std::vector<DirectoryRefItem>::iterator dir);
  void sort_directory();
  void glob_path(const std::filesystem::path& path, BrowserItem& item);
  void render_item(const std::filesystem::path& root_path, BrowserItem& item);
  void render();
};

extern BrowserWindow g_browser;
}  // namespace wb