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
    float frame_width = 0.0f;
    ImVec2 extra_padding = {};
};

static bool begin_dockable_window(const char* title, bool* p_open = nullptr,
                                  ImGuiWindowFlags flags = 0) {
    auto state_storage = ImGui::GetStateStorage();
    auto hide_background = state_storage->GetBoolRef(ImGui::GetID((const void*)title));
    float border_size = 1.0f;
    if (*hide_background) {
        flags |= ImGuiWindowFlags_NoBackground;
        border_size = 0.0f;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_size);
    bool ret = ImGui::Begin(title, p_open, flags);
    ImGuiDockNode* node = ImGui::GetWindowDockNode();
    if (ret && node) {
        if (node->HostWindow) {
            // Don't draw background when the background is already drawn by the host window
            *hide_background = (node->HostWindow->Flags & ImGuiWindowFlags_NoBackground) ==
                               ImGuiWindowFlags_NoBackground;
        } else {
            *hide_background = false;
        }
    } else {
        *hide_background = false;
    }
    ImGui::PopStyleVar();
    return ret;
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
static bool hseparator_resizer(T id, float* size, float default_size, float min_size = 0.0f,
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

template <typename T>
static bool slider2(const SliderProperties& properties, const char* str_id, const ImVec2& size,
                    const ImColor& color, T* value, T min, T max, T default_value = 1.0f,
                    const char* format = "%.2f") {
    ImGuiWindow* window = GImGui->CurrentWindow;
    ImVec2 cursor_pos = window->DC.CursorPos;
    // ImVec2 padded_size(size.x - properties.extra_padding.x, size.y - properties.extra_padding.y);
    // cursor_pos.x += properties.extra_padding.x;
    // cursor_pos.y += properties.extra_padding.y;

    ImRect bb(ImVec2(cursor_pos.x, cursor_pos.y), ImVec2(cursor_pos.x, cursor_pos.y) + size);
    ImGuiID id = ImGui::GetID(str_id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    bool dragging = held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2& mouse_pos = g.IO.MousePos;
    ImU32 frame_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImU32 grab_col = ImGui::GetColorU32(color.Value);
    T range = max - min;
    T normalized_value = (*value - min) / range;
    float frame_width = std::max(properties.frame_width, 3.0f);
    ImVec2 grab_size;

    if (properties.grab_shape == SliderGrabShape::Rectangle) {
        grab_size = properties.grab_size;
        grab_size.x = math::min(properties.grab_size.x, size.x);
    } else {
        float diameter = math::min(properties.grab_size.x, properties.grab_size.y);
        grab_size.x = diameter;
        grab_size.y = diameter;
    }

    float scroll_height = size.y - grab_size.y;
    float inv_scroll_height = 1.0f / scroll_height;
    // Log::debug("{}", normalized_value);

    if (ImGui::IsItemActivated())
        g.SliderGrabClickOffset =
            mouse_pos.y - ((1.0f - (float)normalized_value) * scroll_height + cursor_pos.y);

    if (held) {
        float val = (mouse_pos.y - cursor_pos.y - g.SliderGrabClickOffset) * inv_scroll_height;
        normalized_value = std::clamp(1.0f - val, 0.0f, 1.0f);
        *value = normalized_value * range + min;
        // ImGui::SetNextWindowPos(ImVec2())
        ImGui::BeginTooltip();
        ImGui::Text(format, *value);
        ImGui::EndTooltip();
    }

    float grab_pos = (1.0f - (float)normalized_value) * scroll_height;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float center_x = cursor_pos.x + size.x * 0.5f;
    ImVec2 frame_rect_min(center_x - frame_width * 0.5f, cursor_pos.y + grab_size.y * 0.5f);
    ImVec2 frame_rect_max(frame_rect_min.x + frame_width, frame_rect_min.y + scroll_height);

    // draw_list->AddRect(cursor_pos, bb.Max, frame_col);
    draw_list->AddRectFilled(frame_rect_min, frame_rect_max, frame_col);

    if (properties.grab_shape == SliderGrabShape::Rectangle) {
        ImVec2 grab_rect_min(center_x - grab_size.x * 0.5f, cursor_pos.y + math::round(grab_pos));
        ImVec2 grab_rect_max(grab_rect_min.x + grab_size.x, grab_rect_min.y + grab_size.y);
        draw_list->AddRectFilled(grab_rect_min, grab_rect_max, grab_col, properties.grab_roundness);
        draw_list->AddLine(
            ImVec2(grab_rect_min.x + 2.0f, grab_rect_min.y + grab_size.y * 0.5f),
            ImVec2(grab_rect_min.x + grab_size.x - 2.0f, grab_rect_min.y + grab_size.y * 0.5f),
            0xFFFFFFFF, 3.0f);
    } else {
        float radius1 = grab_size.x * 0.5f;
        float radius2 = grab_size.x * 0.25f;
        float pos_y = math::round(grab_pos) + radius1;
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius1, grab_col);
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius2, 0xFFFFFFFF);
    }

    if (dragging) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta();
        if (delta.y != 0.0f)
            return true;
    }

    return false;
}

void song_position();
bool param_drag_db(const char* str_id, float* value, float speed = 0.1f, float min_db = -72.0f,
                   float max_db = 6.0f, const char* format = "%.2fdB",
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