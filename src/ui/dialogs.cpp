#include "dialogs.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include <imgui_stdlib.h>

namespace wb {

static bool is_mouse_clicked_outside_popup() {
    return !ImGui::IsAnyItemActive() &&
           (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
           !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                                   ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

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

ConfirmDialogFlags change_color_dialog(const char* str, const ImColor& previous, ImColor* color) {
    return 0;
}

ConfirmDialogFlags rename_dialog(const char* str, const std::string& previous, std::string* name) {
    ConfirmDialogFlags ret = ConfirmDialog::None;

    if (ImGui::BeginPopup(str, ImGuiWindowFlags_NoMove)) {
        bool is_enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

        ImGui::TextUnformatted("Rename");

        if (ImGui::InputTextWithHint("##new_clip_name", "New name", name))
            ret = ConfirmDialog::ValueChanged;

        if (ImGui::IsItemDeactivated() && is_enter_pressed) {
            ImGui::CloseCurrentPopup();
            ret = ConfirmDialog::Ok;
        }

        if (ImGui::Button("Ok")) {
            ImGui::CloseCurrentPopup();
            ret = ConfirmDialog::Ok;
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            ret = ConfirmDialog::Cancel;
            *name = previous;
        }

        if (is_mouse_clicked_outside_popup()) {
            ret = ConfirmDialog::Cancel;
            *name = previous;
        }

        ImGui::EndPopup();
    }

    return ret;
}

} // namespace wb