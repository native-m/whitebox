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

bool popup_confirm(const char* str, const char* msg) {
    bool ret = false;
    if (ImGui::BeginPopupModal(str, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(msg);
        ImGui::Separator();
        ret = ImGui::Button("Ok", ImVec2(120.0f, 0.0f));
        if (ret)
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return ret;
}
} // namespace wb