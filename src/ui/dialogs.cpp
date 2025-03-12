#include "dialogs.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/bit_manipulation.h"
#include "core/debug.h"

namespace wb {

static bool is_mouse_clicked_outside_popup() {
  return !ImGui::IsAnyItemActive() &&
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
         !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

ConfirmDialogFlags confirm_dialog(const char* str, const char* msg, ConfirmDialogFlags flags) {
  ConfirmDialogFlags ret = ConfirmDialog::None;
  ImGuiWindowClass window_class;
  window_class.ViewportFlagsOverrideSet |=
      ImGuiViewportFlags_TopMost | ImGuiViewportFlags_NoAutoMerge;  // This popup should be the top-most window
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

    if (ImGui::Button("Ok", ImVec2(100.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
      ret = ConfirmDialog::Ok;
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
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

ConfirmDialogFlags color_picker_dialog(const char* str, const ImColor& previous, ImColor* color) {
  ConfirmDialogFlags ret = ConfirmDialog::None;
  constexpr ImGuiColorEditFlags color_picker_flags = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview;

  if (ImGui::BeginPopup(str, ImGuiWindowFlags_NoMove)) {
    ImGui::TextUnformatted("Change color");

    if (ImGui::ColorPicker3("##clip_color_picker", std::bit_cast<float*>(color), color_picker_flags)) {
      ret = ConfirmDialog::ValueChanged;
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Current");
    ImGui::ColorButton("##current", *color, ImGuiColorEditFlags_NoPicker, ImVec2(60, 40));
    ImGui::TextUnformatted("Previous");
    if (ImGui::ColorButton("##previous", previous, ImGuiColorEditFlags_NoPicker, ImVec2(60, 40))) {
      *color = previous;
      ret = ConfirmDialog::ValueChanged;
    }
    ImGui::EndGroup();

    ImGui::Separator();

    if (ImGui::Button("Ok", ImVec2(100.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
      ret = ConfirmDialog::Ok;
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
      ret = ConfirmDialog::Cancel;
      *color = previous;
    }

    if (is_mouse_clicked_outside_popup()) {
      ret = ConfirmDialog::Cancel;
      *color = previous;
    }

    ImGui::EndPopup();
  }

  return ret;
}

}  // namespace wb