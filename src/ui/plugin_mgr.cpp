#include "plugin_mgr.h"
#include <imgui.h>

namespace wb {
GuiPluginManager g_plugin_manager;

enum PluginManagerColumnID {
    PluginManagerColumnID_Name,
    PluginManagerColumnID_Vendor,
    PluginManagerColumnID_Type,
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

    ImGui::Button("Scan Plugins");

    static constexpr auto table_flags = ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable |
                                        ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("plugin_table", 5, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Name);
        ImGui::TableSetupColumn("Vendor", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Vendor);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 0.0f, PluginManagerColumnID_Type);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                PluginManagerColumnID_Version);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.0f, PluginManagerColumnID_Path);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty) {
                // TODO: Update ordering
                sort_specs->SpecsDirty = false;
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void GuiPluginManager::update_plugin_info_data(ImGuiTableSortSpecs* sort_specs) {
}
} // namespace wb