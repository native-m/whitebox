#include "history.h"
#include "controls.h"
#include "command_manager.h"

namespace wb {
void render_history_window() {
    static bool open = true;
    if (!open)
        return;

    if (!controls::begin_dockable_window("History", &open)) {
        ImGui::End();
        return;
    }

    ImVec2 space = ImGui::GetContentRegionAvail();

    if (ImGui::BeginListBox("##history_listbox",
                            ImVec2(-FLT_MIN, space.y))) {
        for (uint32_t i = 0; i < g_cmd_manager.size; i++) {
            HistoryItem& item = g_cmd_manager.items[i];
            ImGui::Selectable(item.name.c_str());
        }
        ImGui::EndListBox();
    }

    ImGui::End();
}
} // namespace wb