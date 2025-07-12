#pragma once

#include <SDL3/SDL_dialog.h>

#include <filesystem>
#include <nfd.hpp>
#include <optional>
#include <utility>

#include "core/span.h"

namespace wb {

enum class FileDialogType {
  OpenFile,
  SaveFile,
  PickFolder,
};

enum class FileDialogStatus {
  None,
  Accepted,
  Cancelled,
  Failed,
};

void file_dialog_handle_event(void* event_data1, void* event_data2);
void file_dialog_cleanup();

void pick_folder_dialog_async(const char* id, const char* default_location = nullptr);

void open_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location = nullptr);

void save_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location = nullptr);

FileDialogStatus get_file_dialog_payload(const char* id, FileDialogType type, const std::filesystem::path** file);

}  // namespace wb