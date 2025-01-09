#include "plugin_mgr.h"
#include <algorithm>
#include <imgui.h>

namespace wb {
GuiPluginManager g_plugin_manager;

enum PluginManagerColumnID {
    // PluginManagerColumnID_Select,
    PluginManagerColumnID_Name,
    PluginManagerColumnID_Vendor,
    PluginManagerColumnID_Format,
    PluginManagerColumnID_Version,
    PluginManagerColumnID_Path,
};

void GuiPluginManager::render() {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugin Manager", &open, ImGuiWindowFlags_NoDocking)) {
        return;
    }

    bool force_refresh = false;
    bool rescan_plugins = false;
    if (ImGui::Button("Scan Plugins")) {
        scan_plugins();
        force_refresh = true;
        rescan_plugins = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
        force_refresh = true;

    static constexpr auto table_flags = ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable | 
                                        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable |
                                        ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("plugin_table", 5, table_flags)) {
        if (rescan_plugins)
            ImGui::TableSetColumnWidthAutoAll(ImGui::GetCurrentTable());

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Name);
        ImGui::TableSetupColumn("Vendor", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Vendor);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Format);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Version);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.0f, PluginManagerColumnID_Path);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty || force_refresh) {
                update_plugin_info_data(sort_specs);
                sort_specs->SpecsDirty = false;
            }
        }

        for (uint32_t id = 0; const auto& plugin_info : plugin_infos) {
            ImGui::PushID(id);
            ImGui::TableNextRow();
            
            bool selected = selected_plugins[id];
            ImGui::TableNextColumn();
            if (ImGui::Selectable(plugin_info.name.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                if (selected) {
                    selected_plugins.unset(id);
                    num_selected_plugins--;
                } else {
                    selected_plugins.set(id);
                    num_selected_plugins++;
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(plugin_info.vendor.c_str());

            ImGui::TableNextColumn();
            switch (plugin_info.format) {
                case PluginFormat::Native:
                    ImGui::TextUnformatted("Native");
                    break;
                case PluginFormat::VST3:
                    ImGui::TextUnformatted("VST3");
                    break;
                default:
                    WB_UNREACHABLE();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(plugin_info.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(plugin_info.path.c_str());
            ImGui::PopID();
            id++;
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void GuiPluginManager::update_plugin_info_data(ImGuiTableSortSpecs* sort_specs) {
    Vector<PluginInfo> plugin_info_data = load_plugin_info();
    auto sort_predicate = [sort_specs](const PluginInfo& a, const PluginInfo& b) {
        const ImGuiTableColumnSortSpecs* sort_spec = &sort_specs->Specs[0];
        ImGuiSortDirection sort_dir = sort_spec->SortDirection;
        auto ch_pred = [sort_dir](char32_t a, char32_t b) {
            a = std::tolower(a);
            b = std::tolower(b);
            return sort_dir == ImGuiSortDirection_Ascending ? a < b : a > b;
        };
        switch (sort_spec->ColumnUserID) {
            case PluginManagerColumnID_Name:
                return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                                    ch_pred);
            case PluginManagerColumnID_Vendor:
                return std::lexicographical_compare(a.vendor.begin(), a.vendor.end(), b.vendor.begin(), b.vendor.end(),
                                                    ch_pred);
            case PluginManagerColumnID_Format:
                return sort_dir == ImGuiSortDirection_Ascending ? a.format < b.format : a.format > b.format;
            case PluginManagerColumnID_Path:
                return std::lexicographical_compare(a.path.begin(), a.path.end(), b.path.begin(), b.path.end(),
                                                    ch_pred);
            default:
                WB_UNREACHABLE();
        }
        return true;
    };
    std::sort(plugin_info_data.begin(), plugin_info_data.end(), sort_predicate);
    selected_plugins.clear();
    selected_plugins.resize(plugin_info_data.size());
    plugin_infos = std::move(plugin_info_data);
}
} // namespace wb