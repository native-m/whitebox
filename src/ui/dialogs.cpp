#include "dialogs.h"
#include "engine/engine.h"
#include <imgui.h>

namespace wb {
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