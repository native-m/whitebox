#include "dialogs.h"
#include "core/bit_manipulation.h"
#include <imgui.h>

namespace wb {

ConfirmDialogFlags confirm_dialog(const char* str, const char* msg, ConfirmDialogFlags flags) {
    ConfirmDialogFlags ret {};
    ImGuiWindowClass window_class;
    window_class.ViewportFlagsOverrideSet |= ImGuiViewportFlags_TopMost; // This popup should be the top-most window
    ImGui::SetNextWindowClass(&window_class);
    ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags popup_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginPopupModal(str, nullptr, popup_flags)) {
        ImGui::TextUnformatted(msg);
        ImGui::Separator();
        if (contain_bit(flags, ConfirmDialog::Yes)) {
            if (ImGui::Button("Yes", ImVec2(100.0f, 0.0f))) {
                ret = ConfirmDialog::Yes;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (contain_bit(flags, ConfirmDialog::Ok)) {
            if (ImGui::Button("Ok", ImVec2(100.0f, 0.0f))) {
                ret = ConfirmDialog::Ok;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (contain_bit(flags, ConfirmDialog::No)) {
            if (ImGui::Button("No", ImVec2(100.0f, 0.0f))) {
                ret = ConfirmDialog::No;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (contain_bit(flags, ConfirmDialog::Cancel)) {
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                ret = ConfirmDialog::Cancel;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        ImGui::EndPopup();
    }

    return ret;
}

} // namespace wb