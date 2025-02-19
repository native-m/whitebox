#include "dialogs.h"
#include <imgui.h>

namespace wb {

bool popup_confirm(const char* str, const char* msg) {
    bool ret = false;
    ImGuiWindowClass window_class;
    window_class.ViewportFlagsOverrideSet |= ImGuiViewportFlags_TopMost; // This popup should be the top-most window
    ImGui::SetNextWindowClass(&window_class);
    ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags popup_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginPopupModal(str, nullptr, popup_flags)) {
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