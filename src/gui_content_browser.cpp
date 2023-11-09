#include "gui_content_browser.h"
#include "global_state.h"
#include "core/debug.h"
#include <imgui.h>
#include <ranges>
#include <string_view>
#include <nfd.hpp>

namespace fs = std::filesystem;

namespace wb
{
    GUIContentBrowser g_gui_content_browser;

    GUIContentBrowser::GUIContentBrowser()
    {
    }

    void GUIContentBrowser::add_directory(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path) && !directory_set.contains(path)) {
            auto [iterator, already_exists] = directory_set.emplace(path);
            if (already_exists) {
                directories.emplace_back(iterator,
                                         ContentBrowserItem{
                                            .name = path.stem().u8string(),
                                            .root_dir = true
                                         });
            }
        }
    }

    void GUIContentBrowser::sort_directory()
    {
        std::stable_sort(directories.begin(),
                         directories.end(),
                         [](const DirectoryRefItem& a, const DirectoryRefItem& b) {
                             return a.second.name < b.second.name;
                         });
    }

    void GUIContentBrowser::glob_path(const std::filesystem::path& path, ContentBrowserItem& item)
    {
        item.dir_items.emplace();
        item.file_items.emplace();
        for (const auto& dir_entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            if (dir_entry.is_directory()) {
                ContentBrowserItem& child_item =
                    item.dir_items->emplace_back(ContentBrowserItem::Directory, &item,
                                                 dir_entry.path().filename().generic_u8string());
            }
            else if (dir_entry.is_regular_file()) {
                ContentBrowserItem& child_item =
                    item.file_items->emplace_back(ContentBrowserItem::File, &item,
                                                  dir_entry.path().filename().generic_u8string(),
                                                  FileSize(dir_entry.file_size()));
            }
        }
    }

    void GUIContentBrowser::render_item(const std::filesystem::path& root_path, ContentBrowserItem& item)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        if (item.type == ContentBrowserItem::Directory) {
            ImGui::PushID((const void*)item.name.data());
            bool directory_open = ImGui::TreeNodeEx("##browser_item", ImGuiTreeNodeFlags_SpanFullWidth, (const char*)item.name.data());
            bool dir_activated = ImGui::IsItemActivated();
            ImGui::PopID();

            if (dir_activated) {
                if (!item.open) {
                    auto path_from_root_dir = item.get_file_path(root_path);
                    glob_path(path_from_root_dir, item);
                }
                else if (item.open) {
                    item.dir_items.reset();
                    item.file_items.reset();
                }

                item.open = !item.open;
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
        }
        else {
            ImGui::PushID((const void*)item.name.data());
            ImGui::TreeNodeEx("##browser_item",
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth,
                              (const char*)item.name.data());
            
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                // TODO: show item context menu
            }

            if (ImGui::BeginDragDropSource()) {
                ContentBrowserFilePayload payload{
                    .root_dir = &root_path,
                    .item = &item
                };
                ImGui::SetDragDropPayload("WB_FILEDROP", &payload, sizeof(ContentBrowserFilePayload), ImGuiCond_Once);
                ImGui::Text((const char*)item.name.data());
                ImGui::EndDragDropSource();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%.2f %s", item.size.value, item.size.unit);

            ImGui::PopID();
        }
    }

    void GUIContentBrowser::render()
    {
        if (!g_show_content_browser)
            return;

        // When docked, there is no reason to draw the window background
        if (!ImGui::Begin("Browser", &g_show_content_browser, docked ? ImGuiWindowFlags_NoBackground : 0)) {
            ImGui::End();
            return;
        }
        docked = ImGui::IsWindowDocked();

        if (ImGui::Button("Add Folder")) {
            NFD::UniquePathU8 path;
            nfdresult_t result = NFD::PickFolder(path);
            switch (result) {
                case NFD_OKAY:
                {
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

        static constexpr auto table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        auto default_item_spacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(default_item_spacing.x, 0.0f));
        if (ImGui::BeginTable("content_browser", 2, table_flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0f);
            ImGui::TableHeadersRow();

            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
            for (auto& [path, item] : directories)
                render_item(*path, item);
            ImGui::PopStyleVar();

            ImGui::EndTable();

            if (ImGui::BeginDragDropTarget()) {
                static constexpr auto drag_drop_flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                if (ImGui::AcceptDragDropPayload("ExternalFileDrop", drag_drop_flags)) {
                    for (const auto& item : g_item_dropped)
                        add_directory(item);
                    sort_directory();
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::PopStyleVar();

        ImGui::End();
    }
}