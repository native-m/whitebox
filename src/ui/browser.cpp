#include "browser.h"

#include <cstdint>

#include "controls.h"
#include "core/algorithm.h"
#include "core/fs.h"
#include "dialogs.h"
#include "dsp/sample.h"
#include "engine/asset.h"
#include "engine/engine2.h"
#include "file_dialog.h"
#include "file_dropper.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "window.h"

namespace fs = std::filesystem;

namespace wb {

static std::pair<double, double> get_item_content_info(const std::filesystem::path& path) {
  if (auto length = Sample::get_file_info(path)) {
    return {
      (double)length->sample_count,
      (double)length->rate,
    };
  }
  return {};
}

BrowserWindow::BrowserWindow() {
}

void BrowserWindow::add_directory(const std::filesystem::path& path) {
  if (fs::is_directory(path) && !directory_set.contains(path)) {
    auto [iterator, already_exists] = directory_set.emplace(path);
    if (already_exists) {
      directories.emplace_back(
          iterator,
          BrowserItem{
            .name = path.filename().u8string(),
            .root_path = path.parent_path(),
            .root_dir = true,
          });
    }
  }
}

void BrowserWindow::remove_directory(std::vector<BrowserWindow::DirectoryRefItem>::iterator dir) {
  directory_set.erase(dir->first);
  directories.erase(dir);
}

void BrowserWindow::sort_directory() {
  std::stable_sort(directories.begin(), directories.end(), [](const DirectoryRefItem& a, const DirectoryRefItem& b) {
    return a.second.name < b.second.name;
  });
}

void BrowserWindow::glob_path(const std::filesystem::path& path, BrowserItem& item) {
  Vector<DirectoryEntry> entries = enumerate_directory(path);
  for (const auto& dir_entry : entries) {
    if (dir_entry.is_directory()) {
      BrowserItem& child_item = item.subitems.emplace_back(
          BrowserItem::Directory, BrowserItem::Unknown, &item, FileSize(), dir_entry.name.filename().generic_u8string());
      // if (last_item)
      //   last_item->next_item = &child_item;
      // last_item = &child_item;
    } else if (dir_entry.is_file()) {
      std::filesystem::path ext{ dir_entry.name.extension() };
      BrowserItem::FileType file_type{};
      if (any_of(ext, ".wav", ".wave", ".aiff", ".mp3", ".ogg", ".aifc", ".aif", ".iff", ".8svx")) {
        file_type = BrowserItem::Sample;
      } else if (any_of(ext, ".mid", ".midi")) {
        file_type = BrowserItem::Midi;
      } else {
        continue;
      }
      BrowserItem& child_item = item.subitems.emplace_back(
          BrowserItem::File, file_type, &item, FileSize(dir_entry.size), dir_entry.name.generic_u8string());
    }
  }
}

void BrowserWindow::handle_navigation() {
  if (selected_item) {
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      if (selected_item->is_directory() && selected_item->open) {
        if (!selected_item->subitems.empty()) {
          // Navigate to the sub item
          BrowserItem* root = selected_item;
          selected_item = &selected_item->subitems.front();
          selected_item_path = selected_item->get_file_path();
          if (selected_item->is_sample()) {
            play_file = true;
          }
        }
      } else {
        BrowserItem* parent = selected_item->parent;
        BrowserItem* first_item = &parent->subitems.front();
        intptr_t next_index = (selected_item - first_item) + 1;  // get next index
        if (parent != nullptr) {
          if (next_index >= parent->subitems.size()) {
            // Navigate to the grandparent
            BrowserItem* grandparent = parent->parent;
            if (grandparent) {
              first_item = &grandparent->subitems.front();
              next_index = (parent - first_item) + 1;
              if (next_index < grandparent->subitems.size()) {
                selected_item = &grandparent->subitems[next_index];
                selected_item_path = selected_item->get_file_path();
                if (selected_item->is_sample()) {
                  play_file = true;
                }
              }
            }
          } else {
            // Navigate to the next item
            selected_item = &parent->subitems[next_index];
            selected_item_path = selected_item->get_file_path();
            if (selected_item->is_sample()) {
              play_file = true;
            }
          }
        }
      }
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      BrowserItem* parent = selected_item->parent;
      if (parent) {
        BrowserItem* first_item = &parent->subitems.front();
        intptr_t prev_index = (selected_item - first_item) - 1;  // get previous index
        if (prev_index < 0) {
          // Navigate to the parent
          selected_item = parent;
          selected_item_path = selected_item->get_file_path();
          if (selected_item->is_sample()) {
            play_file = true;
          }
        } else {
          BrowserItem* prev_item = &parent->subitems[prev_index];
          if (prev_item->is_directory() && prev_item->open) {
            selected_item = &prev_item->subitems.back();
            selected_item_path = selected_item->get_file_path();
            if (selected_item->is_sample()) {
              play_file = true;
            }
          } else {
            selected_item = prev_item;
            selected_item_path = selected_item->get_file_path();
            if (selected_item->is_sample()) {
              play_file = true;
            }
          }
        }
      }
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      if (selected_item->is_directory() && !selected_item->open) {
        selected_item->set_open = true;  // Open this directory
      } else if (selected_item->is_sample()) {
        play_file = true;
      }
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      if (selected_item->is_directory() && selected_item->open) {
        selected_item->set_close = true;
      }
    }
  }
}

void BrowserWindow::render_item(const std::filesystem::path& root_path, BrowserItem& item) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);

  if (item.type == BrowserItem::Directory) {
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAllColumns;

    if (&item == selected_item)
      flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID((const void*)item.name.data());

    if (item.set_open) {
      ImGui::SetNextItemOpen(true);
      item.set_open = false;
    }

    if (item.set_close) {
      ImGui::SetNextItemOpen(false);
      item.set_close = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GImGui->Style.FramePadding.x, 2.0f));
    bool directory_open = ImGui::TreeNodeEx("##browser_dir", flags, (const char*)item.name.data());
    ImGui::PopStyleVar();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      selected_item_path = item.get_file_path();
      selected_item = &item;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      selected_item_path = item.get_file_path();
      context_menu_item = &item;
      open_context_menu = true;
    }

    if (!item.open && directory_open) {
      auto path_from_root_dir = item.get_file_path();
      glob_path(path_from_root_dir, item);
      item.open = directory_open;
    }

    if (item.open && !directory_open) {
      item.subitems.clear();
      item.subitems.shrink_to_fit();
      item.open = directory_open;
    }

    if (directory_open) {
      if (!item.subitems.empty())
        for (auto& directory_item : item.subitems)
          render_item(root_path, directory_item);
      ImGui::TreePop();
    }
    ImGui::PopID();
  } else {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_SpanAllColumns;

    if (&item == selected_item)
      flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID((const void*)item.name.data());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GImGui->Style.FramePadding.x, 2.0f));
    ImGui::TreeNodeEx("##browser_item", flags, (const char*)item.name.data());
    ImGui::PopStyleVar();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      selected_item_path = item.get_file_path();
      selected_item = &item;
      play_file = true;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      selected_item_path = item.get_file_path();
      context_menu_item = &item;
      open_context_menu = true;
    }

    if (ImGui::BeginDragDropSource()) {
      dragging_item = &item;
      is_dragging_item = true;
      if (last_dragged_item != dragging_item) {
        last_dragged_item = dragging_item;
        if (last_dragged_item != nullptr) {
          auto path = item.get_file_path();
          auto [length, sample_rate] = get_item_content_info(path);
          drop_payload.type = item.file_type;
          drop_payload.content_length = length;
          drop_payload.sample_rate = sample_rate;
          drop_payload.filename = path.filename().generic_string();
          drop_payload.path = std::move(path);
        }
      }

      BrowserFilePayload* payload = &drop_payload;
      ImGui::SetDragDropPayload("WB_FILEDROP", &payload, sizeof(BrowserFilePayload*), ImGuiCond_Once);
      ImGui::TextUnformatted((const char*)item.name.c_str(), (const char*)item.name.c_str() + item.name.size());
      ImGui::EndDragDropSource();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.2f %s", item.size.value, item.size.unit);

    ImGui::PopID();
  }
}

void BrowserWindow::render() {
  if (!controls::begin_window("Browser", &g_browser_window_open)) {
    controls::end_window();
    return;
  }

  if (ImGui::Button("Add Folder")) {
    pick_folder_dialog_async("add_br_folder");
  }

  const std::filesystem::path* folder_path;
  if (auto ret = get_file_dialog_payload("add_br_folder", FileDialogType::PickFolder, &folder_path);
      ret == FileDialogStatus::Accepted) {
    add_directory(*folder_path);
    sort_directory();
  }

  is_dragging_item = false;
  dragging_item = nullptr;

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    handle_navigation();
  }

  static constexpr auto table_flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
  auto default_item_spacing = ImGui::GetStyle().ItemSpacing;
  auto table_size = ImGui::GetContentRegionAvail();
  // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(default_item_spacing.x, 0.0f));
  if (ImGui::BeginTable("content_browser", 2, table_flags, ImVec2(table_size.x, table_size.y - 50.0f))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0f);
    ImGui::TableHeadersRow();

    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
    for (uint32_t i = 0; i < directories.size(); i++) {
      auto& dir = directories[i];
      render_item(*dir.first, dir.second);
      if (context_menu_item == &dir.second && context_menu_item->root_dir)
        selected_root_dir = i;
    }
    ImGui::PopStyleVar();  // ImGuiStyleVar_IndentSpacing

    ImGui::EndTable();

    if (ImGui::BeginDragDropTarget()) {
      static constexpr auto drag_drop_flags =
          ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
      if (ImGui::AcceptDragDropPayload("ExternalFileDrop", drag_drop_flags)) {
        for (const auto& item : g_file_drop)
          add_directory(item);
        sort_directory();
      }
      ImGui::EndDragDropTarget();
    }
  }

  ImVec2 waveform_view_size = ImGui::GetContentRegionAvail();
  if (controls::image_view("waveform_view", waveform_view_size, nullptr)) {
    play_file = true;
  }

  if (!is_dragging_item && last_dragged_item != nullptr) {
    last_dragged_item = nullptr;
  }

  if (selected_item && play_file) {
    if (selected_item->type == BrowserItem::File) {
      if (auto asset = AssetManager::create_or_get_audio_asset(selected_item_path.generic_string())) {
        if (current_audio_asset)
          current_audio_asset->release();
        Engine2::preview_sample(asset);
        current_audio_asset = asset;
      }
    }
  }

  if (open_context_menu) {
    ImGui::OpenPopup("browser_context_menu");
    open_context_menu = false;
  }

  bool confirm_remove_directory = false;

  if (ImGui::BeginPopup("browser_context_menu")) {
    ImGui::MenuItem("Copy path");

    if (ImGui::MenuItem("Open parent folder")) {
      explore_folder(selected_item_path.parent_path());
    }

    if (context_menu_item->type == BrowserItem::Directory) {
      if (ImGui::MenuItem("Open directory")) {
        explore_folder(selected_item_path);
      }
    } else {
      if (ImGui::MenuItem("Locate file")) {
        locate_file(selected_item_path);
      }
    }

    if (context_menu_item->root_dir) {
      ImGui::Separator();
      if (ImGui::MenuItem("Remove from browser")) {
        confirm_remove_directory = true;
      }
    }

    ImGui::EndPopup();
  } else {
    context_menu_item = nullptr;
  }

  if (confirm_remove_directory) {
    ImGui::OpenPopup("Remove from browser##remove_from_browser");
  }

  if (auto ret = confirm_dialog(
          "Remove from browser##remove_from_browser",
          "Are you sure you want to remove this directory from browser?",
          ConfirmDialog::YesNo)) {
    if (ret == ConfirmDialog::Yes) {
      remove_directory(directories.begin() + selected_root_dir);
    }
  }

  controls::end_window();
  play_file = false;
}

BrowserWindow g_browser;
}  // namespace wb