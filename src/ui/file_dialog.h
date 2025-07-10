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

void init_file_dialog();
void shutdown_file_dialog();
void file_dialog_handle_event(void* event_data);
void file_dialog_cleanup();

std::optional<std::filesystem::path> pick_folder_dialog();

std::optional<std::filesystem::path> open_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);

std::optional<std::filesystem::path> save_file_dialog(std::initializer_list<nfdu8filteritem_t> filter);

void pick_folder_dialog_async(const char* id, const char* default_location);

void open_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location);

void save_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location);

std::optional<std::filesystem::path> accept_file_dialog_payload(const char* id, FileDialogType type);

}  // namespace wb