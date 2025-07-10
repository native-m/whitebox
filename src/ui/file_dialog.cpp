#include "file_dialog.h"

#include "core/vector.h"
#include "window_manager.h"
#include "app_event.h"

namespace wb {

struct FileDialogEventData {
  const char* id;
  FileDialogType type;
  std::filesystem::path file;
};

static FileDialogEventData* file_dialog_data;
static bool block_next_dialog = false;

template<FileDialogType Type>
static void file_dialog_callback(void* userdata, const char* const* filelist, int filter) {
  if (!filelist) {
    return;
  } else if (!*filelist) {
    return;
  }
  FileDialogEventData* fd = new FileDialogEventData();
  fd->id = (const char*)userdata;
  fd->type = Type;
  fd->file = *filelist;
  block_next_dialog = false;
  app_event_push(AppEvent::file_dialog, fd);
}

void init_file_dialog() {
  NFD::Init();
}

void shutdown_file_dialog() {
  NFD::Quit();
}

void file_dialog_handle_event(void* event_data) {
  file_dialog_data = (FileDialogEventData*)event_data;
}

void file_dialog_cleanup() {
  if (file_dialog_data) {
    delete file_dialog_data;
    file_dialog_data = nullptr;
  }
}

std::optional<std::filesystem::path> pick_folder_dialog() {
  NFD::UniquePathU8 path;
  nfdresult_t result = NFD::PickFolder(path);
  switch (result) {
    case NFD_OKAY: return std::filesystem::path(path.get());
    default: break;
  }
  return {};
}

std::optional<std::filesystem::path> open_file_dialog(std::initializer_list<nfdu8filteritem_t> filter) {
  NFD::UniquePathU8 path;
  nfdresult_t result = NFD::OpenDialog(path, filter.begin(), (nfdfiltersize_t)filter.size());
  switch (result) {
    case NFD_OKAY: return std::filesystem::path(path.get());
    default: break;
  }
  return {};
}

std::optional<std::filesystem::path> save_file_dialog(std::initializer_list<nfdu8filteritem_t> filter) {
  NFD::UniquePathU8 path;
  nfdresult_t result = NFD::SaveDialog(path, filter.begin(), (nfdfiltersize_t)filter.size());
  switch (result) {
    case NFD_OKAY: return std::filesystem::path(path.get());
    default: break;
  }
  return {};
}

void pick_folder_dialog_async(const char* id, const char* default_location) {
  if (block_next_dialog)
    return;
  block_next_dialog = true;
  SDL_ShowOpenFolderDialog(
      file_dialog_callback<FileDialogType::PickFolder>, (void*)id, wm_get_main_window(), default_location, false);
}

void open_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location) {
  if (block_next_dialog)
    return;
  block_next_dialog = true;
  SDL_ShowOpenFileDialog(
      file_dialog_callback<FileDialogType::OpenFile>,
      (void*)id,
      wm_get_main_window(),
      filter.begin(),
      filter.size(),
      default_location,
      false);
}

void save_file_dialog_async(
    const char* id,
    std::initializer_list<SDL_DialogFileFilter> filter,
    const char* default_location) {
  if (block_next_dialog)
    return;
  block_next_dialog = true;
  SDL_ShowSaveFileDialog(
      file_dialog_callback<FileDialogType::SaveFile>,
      (void*)id,
      wm_get_main_window(),
      filter.begin(),
      filter.size(),
      default_location);
}

std::optional<std::filesystem::path> accept_file_dialog_payload(const char* id, FileDialogType type) {
  if (file_dialog_data == nullptr)
    return {};
  if (file_dialog_data->id != id && file_dialog_data->type != type)
    return {};
  return file_dialog_data->file;
}

}  // namespace wb