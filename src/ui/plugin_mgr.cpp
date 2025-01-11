#include "plugin_mgr.h"
#include "core/bit_manipulation.h"
#include "core/core_math.h"
#include "ui/dialogs.h"
#include <algorithm>
#include <imgui_stdlib.h>

namespace wb {
GuiPluginManager g_plugin_manager;

enum PluginManagerColumnID {
    PluginManagerColumnID_Name,
    PluginManagerColumnID_Vendor,
    PluginManagerColumnID_Type,
    PluginManagerColumnID_Format,
    PluginManagerColumnID_Version,
    PluginManagerColumnID_Hidden,
    PluginManagerColumnID_Path,
};

static uint32_t get_plugin_flag_type(uint32_t flag) {
    switch (flag) {
        case (PluginFlags::Analyzer):
            return 0;
        case (PluginFlags::Effect):
            return 1;
        case (PluginFlags::Instrument):
            return 2;
        case (PluginFlags::Instrument | PluginFlags::Effect):
            return 3;
        default:
            break;
    }
    return 4;
}

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
        pm_scan_plugins();
        force_refresh = true;
        rescan_plugins = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
        force_refresh = true;

    ImGui::SameLine();
    ImGui::PushItemWidth(225.0f);
    if (ImGui::InputTextWithHint("##search", "Search plugin name", &search_text))
        search_timeout = 80.0f / 1000.0f;
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("X")) {
        if (search_text.size() != 0) {
            search_text.clear();
            search_timeout = 80.0f / 1000.0f;
        }
    }

    if (num_selected_plugins > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Deselect All")) {
            selected_plugins.clear();
            num_selected_plugins = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
            ImGui::OpenPopup("Delete##delete_plugin_conf");
    }

    if (popup_confirm("Delete##delete_plugin_conf", "Are you sure you want to delete the selected plugins?")) {
        delete_selected();
        force_refresh = true;
    }

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
                                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("plugin_table", 7, table_flags)) {
        if (rescan_plugins)
            ImGui::TableSetColumnWidthAutoAll(ImGui::GetCurrentTable());

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Name);
        ImGui::TableSetupColumn("Vendor", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Vendor);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Type);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Format);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Version);
        ImGui::TableSetupColumn("Hidden", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Hidden);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Path);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty || force_refresh) {
                update_plugin_info_data(sort_specs);
                sort_specs->SpecsDirty = false;
            }
        }

        for (uint32_t id = 0; auto& plugin_info : plugin_infos) {
            ImGui::PushID(id);
            ImGui::TableNextRow();

            bool selected = selected_plugins[id];
            ImGui::TableNextColumn();
            if (ImGui::Selectable(plugin_info.name.c_str(), selected, selectable_flags)) {
                if (selected) {
                    selected_plugin_set.erase(id);
                    selected_plugins.unset(id);
                    num_selected_plugins--;
                } else {
                    selected_plugin_set.insert(id);
                    selected_plugins.set(id);
                    num_selected_plugins++;
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(plugin_info.vendor.c_str(), plugin_info.vendor.c_str() + plugin_info.vendor.size());

            ImGui::TableNextColumn();
            switch (plugin_info.flags & 7) {
                case (PluginFlags::Analyzer):
                    ImGui::TextUnformatted("Anayzer");
                    break;
                case (PluginFlags::Effect):
                    ImGui::TextUnformatted("Effect");
                    break;
                case (PluginFlags::Instrument):
                    ImGui::TextUnformatted("Instrument");
                    break;
                case (PluginFlags::Instrument | PluginFlags::Effect):
                    ImGui::TextUnformatted("Instrument/Effect");
                    break;
                default:
                    ImGui::TextUnformatted("Unknown");
                    break;
            }

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
            ImGui::TextUnformatted(plugin_info.version.c_str(),
                                   plugin_info.version.c_str() + plugin_info.version.size());

            ImGui::TableNextColumn();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            if (ImGui::CheckboxFlags("##hidden", &plugin_info.flags, PluginFlags::Hidden)) {
                pm_update_plugin_info(plugin_info);
            }
            ImGui::PopStyleVar(2);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(plugin_info.path.c_str(), plugin_info.path.c_str() + plugin_info.path.size());
            ImGui::PopID();
            id++;
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void GuiPluginManager::update_plugin_info_data(ImGuiTableSortSpecs* sort_specs) {
    Vector<PluginInfo> plugin_info_data = pm_fetch_registered_plugins(search_text);
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
            case PluginManagerColumnID_Type: {
                uint32_t a_type = get_plugin_flag_type(a.flags);
                uint32_t b_type = get_plugin_flag_type(b.flags);
                return sort_dir == ImGuiSortDirection_Ascending ? a_type < b_type : a_type > b_type;
            }
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
    selected_plugin_set.clear();
    selected_plugins.clear();
    selected_plugins.resize(plugin_info_data.size());
    plugin_infos = std::move(plugin_info_data);
    num_selected_plugins = 0;
}

void GuiPluginManager::delete_selected() {
    for (auto id : selected_plugin_set) {
        auto& plugin_info = plugin_infos[id];
        pm_delete_plugin(plugin_info.uid);
    }
}
} // namespace wb