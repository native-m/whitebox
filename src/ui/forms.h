#pragma once

#include "controls.h"
#include <imgui_stdlib.h>
#include <string>

namespace wb {

enum class FormResult {
    None,
    ValueChanged,
    Close
};

static FormResult rename_form(std::string* out_name, const std::string* tmp_name) {
    FormResult ret = FormResult::None;
    bool is_enter_pressed =
        ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
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

static FormResult color_picker_form(ImColor* color, const ImColor& previous_color) {
    constexpr ImGuiColorEditFlags no_preview_color_picker =
        ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview;

    FormResult ret = FormResult::None;
    if (ImGui::ColorPicker3("##clip_color_picker", std::bit_cast<float*>(color),
                            no_preview_color_picker)) {
        ret = FormResult::ValueChanged;
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Text("Current");
    ImGui::ColorButton("##current", *color, ImGuiColorEditFlags_NoPicker, ImVec2(60, 40));
    ImGui::Text("Previous");
    if (ImGui::ColorButton("##previous", previous_color, ImGuiColorEditFlags_NoPicker,
                           ImVec2(60, 40))) {
        *color = previous_color;
        ret = FormResult::ValueChanged;
    }
    ImGui::EndGroup();

    return ret;
}

} // namespace wb