#include "dialogs.h"
#include "engine/engine.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace wb {
bool g_show_project_dialog = false;
void project_info_dialog() {
    ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_Once);
    if (!ImGui::Begin("Project Info", &g_show_project_dialog, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    ImGui::InputText("Author", &g_engine.project_info.author);
    ImGui::InputText("Title", &g_engine.project_info.title);
    ImGui::InputText("Genre", &g_engine.project_info.genre);
    auto space = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("Description", &g_engine.project_info.description,
                              ImVec2(0.0f, space.y));

    ImGui::End();
}
} // namespace wb