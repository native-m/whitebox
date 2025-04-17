#include "file_dialog.h"

namespace wb {

void init_file_dialog() {
  NFD::Init();
}

void shutdown_file_dialog() {
  NFD::Quit();
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

}  // namespace wb