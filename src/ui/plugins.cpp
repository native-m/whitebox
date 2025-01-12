#pragma once

#include "plugins.h"
#include "plugin_mgr.h"
#include "window.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace wb {
GuiPlugins g_plugins_window;

enum PluginsColumnID {
    PluginsColumnID_Name,
    PluginsColumnID_Format,
};

void GuiPlugins::render() {
    ImGui::SetNextWindowSize(ImVec2(300.0f, 500.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", &g_plugins_window_open)) {
        ImGui::End();
        return;
    }

    bool force_refresh = false;
    ImVec2 window_area = ImGui::GetContentRegionAvail();
    ImGui::PushItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##search", "Search plugin name", &search_text))
        search_timeout = 80.0f / 1000.0f;
    if (ImGui::Button("Open plugin manager", ImVec2(window_area.x, 0.0f)))
        g_plugin_mgr_window_open = true;
    ImGui::PopItemWidth();

    if (search_timeout > 0.0f) {
        search_timeout = math::max(search_timeout - GImGui->IO.DeltaTime, 0.0f);
        if (math::near_equal_to_zero(search_timeout)) {
            search_timeout = 0.0f;
            force_refresh = true;
        }
    }

    static constexpr auto selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
    static constexpr auto table_flags = ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable |
                                        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable |
                                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("plugin_table", 2, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginsColumnID_Name);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch, 0.0f, PluginsColumnID_Format);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty || force_refresh) {
                update_plugin_info_data(sort_specs);
                sort_specs->SpecsDirty = false;
            }
        }

        for (uint32_t id = 0; auto& item : items) {
            ImGui::PushID(id);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Selectable(item.name.c_str(), false, selectable_flags);

            if (ImGui::BeginDragDropSource()) {
                PluginItem* payload = &item;
                ImGui::SetDragDropPayload("WB_PLUGINDROP", &payload, sizeof(PluginItem*), ImGuiCond_Once);
                ImGui::Text("Plugin: %s", item.name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableNextColumn();
            switch (item.format) {
                case PluginFormat::Native:
                    ImGui::TextUnformatted("Native");
                    break;
                case PluginFormat::VST3:
                    ImGui::TextUnformatted("VST3");
                    break;
                default:
                    WB_UNREACHABLE();
            }

            ImGui::PopID();
            id++;
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void GuiPlugins::update_plugin_info_data(ImGuiTableSortSpecs* sort_specs) {
    Vector<PluginItem> plugin_info_data;
    pm_fetch_registered_plugins(search_text, &plugin_info_data, [](void* userdata, PluginInfo&& info) {
        Vector<PluginItem>* plugins = (Vector<PluginItem>*)userdata;
        PluginItem& item = plugins->emplace_back(info.format, info.flags, std::move(info.name), std::move(info.vendor));
        std::memcpy(item.uid, info.uid, sizeof(PluginUID));
    });

    auto sort_predicate = [sort_specs](const PluginItem& a, const PluginItem& b) {
        const ImGuiTableColumnSortSpecs* sort_spec = &sort_specs->Specs[0];
        ImGuiSortDirection sort_dir = sort_spec->SortDirection;
        auto ch_pred = [sort_dir](char32_t a, char32_t b) {
            a = std::tolower(a);
            b = std::tolower(b);
            return sort_dir == ImGuiSortDirection_Ascending ? a < b : a > b;
        };
        switch (sort_spec->ColumnUserID) {
            case PluginsColumnID_Name:
                return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                                    ch_pred);
            case PluginsColumnID_Format:
                return sort_dir == ImGuiSortDirection_Ascending ? a.format < b.format : a.format > b.format;
            default:
                WB_UNREACHABLE();
        }
        return true;
    };

    std::sort(plugin_info_data.begin(), plugin_info_data.end(), sort_predicate);
    items = std::move(plugin_info_data);
}

} // namespace wb