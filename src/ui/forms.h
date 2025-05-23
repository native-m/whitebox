#pragma once

#include <imgui_stdlib.h>

#include <string>

#include "controls.h"

namespace wb {

enum class FormResult { None, ValueChanged, Close };

static FormResult rename_form(std::string* out_name, const std::string* tmp_name) {
  FormResult ret = FormResult::None;
  bool is_enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  if (ImGui::InputTextWithHint("##new_clip_name", "New name", out_name))
    ret = FormResult::ValueChanged;
  if (ImGui::IsItemDeactivated() && is_enter_pressed)
    ret = FormResult::Close;
  if (ImGui::Button("Ok"))
    ret = FormResult::Close;
  ImGui::SameLine();
  if (tmp_name)
    if (ImGui::Button("Reset"))
      *out_name = *tmp_name;
  return ret;
}

static FormResult color_picker_form(Color* color, const Color& previous_color) {
  constexpr ImGuiColorEditFlags no_preview_color_picker =
      ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview;

  FormResult ret = FormResult::None;
  if (ImGui::ColorPicker3("##clip_color_picker", std::bit_cast<float*>(color), no_preview_color_picker)) {
    ret = FormResult::ValueChanged;
  }

  ImGui::SameLine();

  ImVec4& prev_color = *std::bit_cast<ImVec4*>(&previous_color);
  ImVec4& curr_color = *std::bit_cast<ImVec4*>(color);
  ImGui::BeginGroup();
  ImGui::TextUnformatted("Current");
  ImGui::ColorButton("##current", curr_color, ImGuiColorEditFlags_NoPicker, ImVec2(60, 40));
  ImGui::TextUnformatted("Previous");
  if (ImGui::ColorButton("##previous", prev_color, ImGuiColorEditFlags_NoPicker, ImVec2(60, 40))) {
    *color = previous_color;
    ret = FormResult::ValueChanged;
  }
  ImGui::EndGroup();

  return ret;
}

}  // namespace wb