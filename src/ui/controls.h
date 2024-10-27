#pragma once

#include "core/debug.h"
#include "engine/audio_param.h"
#include "engine/param_changes.h"
#include "engine/vu_meter.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace wb::controls {

enum class SliderGrabShape {
    Circle,
    Rectangle
};

enum class SliderScale {
    Linear,
    Logarithm
};

struct SliderProperties {
    SliderScale scale = SliderScale::Linear;
    SliderGrabShape grab_shape = SliderGrabShape::Circle;
    ImVec2 grab_size = {};
    float grab_roundness = 0.0f;
    ImVec2 extra_padding = {};
    float frame_width = 0.0f;
    bool with_default_value_tick = false;
};

bool begin_window(const char* title, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);
void end_window();

static float get_item_height() {
    return GImGui->FontSize + GImGui->Style.FramePadding.y * 2.0f + 4.0f;
}

static bool collapse_button(const char* str_id, bool* shown) {
    ImGuiID id = ImGui::GetID(str_id);
    ImGuiStyle& style = GImGui->Style;
    float font_size = ImGui::GetFontSize();
    float padding = style.FramePadding.x;
    ImVec2 cur_pos = GImGui->CurrentWindow->DC.CursorPos;
    cur_pos.y += style.FramePadding.y * 0.5f;
    ImRect bb(cur_pos, ImVec2(cur_pos.x + font_size + padding, cur_pos.y + font_size + padding));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    if (pressed)
        *shown = !*shown;

    auto draw_list = ImGui::GetWindowDrawList();
    if (hovered || held) {
        ImU32 bg_col = ImGui::GetColorU32(!held ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive);
        draw_list->AddCircleFilled(ImVec2(cur_pos.x + (font_size + padding) * 0.5f,
                                          cur_pos.y + (font_size + padding) * 0.5f),
                                   font_size * 0.5f + 1.0f, bg_col);
    }

    ImGui::RenderArrow(draw_list, ImVec2(cur_pos.x + padding * 0.5f, cur_pos.y + padding * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text), *shown ? ImGuiDir_Down : ImGuiDir_Right);

    // draw_list->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_PlotLines));

    return pressed;
}

template <typename T>
static bool resizable_horizontal_separator(T id, float* size, float default_size, float min_size = 0.0f,
                               float max_size = 0.0f) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImVec2 region_avail = ImGui::GetWindowContentRegionMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cur_pos = ImGui::GetCursorScreenPos();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiCol color = ImGuiCol_Separator;
    const float separator_pad = 2.0f;

    ImGui::PushID(id);

    ImGuiID item_id = window->GetID("");
    ImRect bb(cur_pos, ImVec2(cur_pos.x + region_avail.x, cur_pos.y + separator_pad));
    ImGui::ItemSize(ImVec2(region_avail.x, separator_pad));
    if (!ImGui::ItemAdd(bb, item_id)) {
        ImGui::PopID();
        return false;
    }

    bool is_separator_hovered;
    ImGui::ButtonBehavior(bb, item_id, &is_separator_hovered, nullptr, 0);
    bool is_separator_active = ImGui::IsItemActive();

    if (size) {
        if (is_separator_hovered || is_separator_active) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                *size = default_size;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        if (is_separator_active) {
            auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            *size = std::clamp(*size + drag_delta.y, min_size, max_size);
            color = ImGuiCol_SeparatorActive;
        } else if (is_separator_hovered) {
            color = ImGuiCol_SeparatorHovered;
        }
    }

    draw_list->AddLine(ImVec2(cur_pos.x, cur_pos.y + 0.5f),
                       ImVec2(cur_pos.x + region_avail.x, cur_pos.y + 0.5f),
                       ImGui::GetColorU32(color), 2.0f);

    ImGui::PopID();
    return is_separator_active;
}

void song_position();
bool param_drag_db(const char* str_id, float* value, float speed = 0.1f, float min_db = -72.0f,
                   float max_db = 6.0f, const char* format = "%.2fdB",
                   ImGuiSliderFlags flags = ImGuiSliderFlags_Vertical);
bool param_drag_panning(const char* str_id, float* value, float speed = 1.0f,
                    ImGuiSliderFlags flags = ImGuiSliderFlags_Vertical);
bool param_slider_db(const SliderProperties& properties, const char* str_id, const ImVec2& size,
                     const ImColor& color, float* value, const NonLinearRange& db_range,
                     float default_value = 0.0f);
bool mixer_label(const char* caption, const float height, const ImColor& color);
void level_meter_options();
void level_meter(const char* str_id, const ImVec2& size, uint32_t count, VUMeter* channels,
                 LevelMeterColorMode color_mode, bool border = true);
void render_test_controls();
extern bool g_test_control_shown;

} // namespace wb::controls