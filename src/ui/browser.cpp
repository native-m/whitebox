#include "browser.h"
#include "controls.h"
#include "core/algorithm.h"
#include "core/fs.h"
#include "dsp/sample.h"
#include "file_dropper.h"
#include "window.h"
#include <nfd.hpp>

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

GuiBrowser::GuiBrowser() {
}

void GuiBrowser::add_directory(const std::filesystem::path& path) {
    if (fs::is_directory(path) && !directory_set.contains(path)) {
        auto [iterator, already_exists] = directory_set.emplace(path);
        if (already_exists) {
            directories.emplace_back(iterator, BrowserItem {
                                                   .name = path.filename().u8string(),
                                                   .root_dir = true,
                                               });
        }
    }
}

void GuiBrowser::remove_directory(std::vector<GuiBrowser::DirectoryRefItem>::iterator dir) {
    directory_set.erase(dir->first);
    directories.erase(dir);
}

void GuiBrowser::sort_directory() {
    std::stable_sort(directories.begin(), directories.end(), [](const DirectoryRefItem& a, const DirectoryRefItem& b) {
        return a.second.name < b.second.name;
    });
}

void GuiBrowser::glob_path(const std::filesystem::path& path, BrowserItem& item) {
    item.dir_items.emplace();
    item.file_items.emplace();
    for (const auto& dir_entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
        if (dir_entry.is_directory()) {
            BrowserItem& child_item =
                item.dir_items->emplace_back(BrowserItem::Directory, BrowserItem::Unknown, &item, FileSize(),
                                             dir_entry.path().filename().generic_u8string());
        } else if (dir_entry.is_regular_file()) {
            std::filesystem::path filename {dir_entry.path().filename()};
            std::filesystem::path ext {filename.extension()};
            BrowserItem::FileType file_type {};
            if (any_of(ext, ".wav", ".wave", ".aiff", ".mp3", ".ogg", ".aifc", ".aif", ".iff", ".8svx")) {
                file_type = BrowserItem::Sample;
            } else if (any_of(ext, ".mid", ".midi")) {
                file_type = BrowserItem::Midi;
            } else {
                continue;
            }
            BrowserItem& child_item = item.file_items->emplace_back(
                BrowserItem::File, file_type, &item, FileSize(dir_entry.file_size()), filename.generic_u8string());
        }
    }
}

void GuiBrowser::render_item(const std::filesystem::path& root_path, BrowserItem& item) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    if (item.type == BrowserItem::Directory) {
        constexpr ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAllColumns;
        ImGui::PushID((const void*)item.name.data());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GImGui->Style.FramePadding.x, 2.0f));
        bool directory_open = ImGui::TreeNodeEx("##browser_dir", flags, (const char*)item.name.data());
        ImGui::PopStyleVar();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            context_menu_path = item.get_file_path(root_path);
            context_menu_item = &item;
            open_context_menu = true;
        }

        if (!item.open && directory_open) {
            auto path_from_root_dir = item.get_file_path(root_path);
            glob_path(path_from_root_dir, item);
            item.open = directory_open;
        }

        if (item.open && !directory_open) {
            item.dir_items.reset();
            item.file_items.reset();
            item.open = directory_open;
        }

        if (directory_open) {
            if (item.dir_items)
                for (auto& directory_item : *item.dir_items)
                    render_item(root_path, directory_item);
            if (item.file_items)
                for (auto& file_item : *item.file_items)
                    render_item(root_path, file_item);
            ImGui::TreePop();
        }
        ImGui::PopID();
    } else {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                             ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
                                             ImGuiTreeNodeFlags_SpanAllColumns;
        ImGui::PushID((const void*)item.name.data());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GImGui->Style.FramePadding.x, 2.0f));
        ImGui::TreeNodeEx("##browser_item", flags, (const char*)item.name.data());
        ImGui::PopStyleVar();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            context_menu_path = item.get_file_path(root_path);
            context_menu_item = &item;
            open_context_menu = true;
        }

        if (ImGui::BeginDragDropSource()) {
            dragging_item = &item;
            is_dragging_item = true;
            if (last_dragged_item != dragging_item) {
                last_dragged_item = dragging_item;
                if (last_dragged_item != nullptr) {
                    auto path = item.get_file_path(root_path);
                    auto [length, sample_rate] = get_item_content_info(path);
                    drop_payload.type = item.file_type;
                    drop_payload.content_length = length;
                    drop_payload.sample_rate = sample_rate;
                    drop_payload.path = std::move(path);
                }
            }

            BrowserFilePayload* payload = &drop_payload;
            ImGui::SetDragDropPayload("WB_FILEDROP", &payload, sizeof(BrowserFilePayload*), ImGuiCond_Once);
            ImGui::Text((const char*)item.name.data());
            ImGui::EndDragDropSource();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f %s", item.size.value, item.size.unit);

        ImGui::PopID();
    }
}

void GuiBrowser::render() {
    if (!controls::begin_window("Browser", &g_browser_window_open)) {
        controls::end_window();
        return;
    }

    if (ImGui::Button("Add Folder")) {
        NFD::UniquePathU8 path;
        nfdresult_t result = NFD::PickFolder(path);
        switch (result) {
            case NFD_OKAY: {
                std::filesystem::path folder(path.get());
                add_directory(folder);
                sort_directory();
                break;
            }
            case NFD_CANCEL:
                break;
            default:
                break;
        }
    }

    is_dragging_item = false;
    dragging_item = nullptr;

    static constexpr auto table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    auto default_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(default_item_spacing.x, 0.0f));
    if (ImGui::BeginTable("content_browser", 2, table_flags)) {
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
        ImGui::PopStyleVar();

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
    ImGui::PopStyleVar();

    if (!is_dragging_item && last_dragged_item != nullptr) {
        last_dragged_item = nullptr;
    }

    if (open_context_menu) {
        ImGui::OpenPopup("browser_context_menu");
        open_context_menu = false;
    }

    if (ImGui::BeginPopup("browser_context_menu")) {
        ImGui::MenuItem("Copy path");

        if (ImGui::MenuItem("Open parent folder")) {
            explore_folder(context_menu_path.parent_path());
        }

        if (context_menu_item->type == BrowserItem::Directory) {
            if (ImGui::MenuItem("Open directory")) {
                explore_folder(context_menu_path);
            }
        } else {
            if (ImGui::MenuItem("Locate file")) {
                locate_file(context_menu_path);
            }
        }

        if (context_menu_item->root_dir) {
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from browser")) {
                // TODO: Add confirmation dialog before removing
                remove_directory(directories.begin() + selected_root_dir);
            }
        }

        ImGui::EndPopup();
    } else {
        context_menu_item = nullptr;
    }

    controls::end_window();
}

GuiBrowser g_browser;
} // namespace wb