#pragma once

#include "core/debug.h"
#include "engine/audio_param.h"
#include "engine/param_changes.h"
#include "engine/vu_meter.h"
#include "platform/platform.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <numbers>

namespace wb::controls {

enum class SliderGrabShape {
    Circle,
    Rectangle
};

enum class SliderScale {
    Linear,
    Logarithm
};

struct KnobFlags {
    enum {
        NoArc,
        NoPointer,
    };
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

struct KnobProperties {
    ImU32 body_color;
    ImU32 arc_color;
    ImU32 arc_bg_color;
    ImU32 pointer_color;
    float body_size = 1.0f;
    float pointer_min_len = 0.0f;
    float pointer_max_len = 1.0f;
    float min_angle = 0.0f;
    float max_angle = 2.0f * std::numbers::pi_v<float>;
};

static float get_item_height() {
    return GImGui->FontSize + GImGui->Style.FramePadding.y * 2.0f + GImGui->Style.ItemSpacing.y;
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
        draw_list->AddCircleFilled(
            ImVec2(cur_pos.x + (font_size + padding) * 0.5f, cur_pos.y + (font_size + padding) * 0.5f),
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

    draw_list->AddLine(ImVec2(cur_pos.x, cur_pos.y + 0.5f), ImVec2(cur_pos.x + region_avail.x, cur_pos.y + 0.5f),
                       ImGui::GetColorU32(color), 2.0f);

    ImGui::PopID();
    return is_separator_active;
}

template <typename T, typename Range>
    requires NormalizedRange<Range, T>
static bool slider2(const SliderProperties& props, const char* str_id, const ImVec2& size, const ImColor& color,
                    T* value, const Range& range, T default_value = 0.0f, const char* format = "%.2f") {
    ImGuiWindow* window = GImGui->CurrentWindow;
    ImVec2 cursor_pos = window->DC.CursorPos;
    ImRect bb(ImVec2(cursor_pos.x, cursor_pos.y), ImVec2(cursor_pos.x, cursor_pos.y) + size);
    ImGuiID id = ImGui::GetID(str_id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    bool dragging = held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    float frame_width = std::max(props.frame_width, 3.0f);
    ImVec2 grab_size;

    if (props.grab_shape == SliderGrabShape::Rectangle) {
        grab_size = props.grab_size;
        grab_size.x = math::min(props.grab_size.x, size.x);
    } else {
        float diameter = math::min(props.grab_size.x, props.grab_size.y);
        grab_size.x = diameter;
        grab_size.y = diameter;
    }

    ImGuiContext& g = *ImGui::GetCurrentContext();
    T normalized_value = (T)range.plain_to_normalized((float)*value);
    const float scroll_height = size.y - grab_size.y;
    const float inv_scroll_height = 1.0f / scroll_height;
    const ImVec2& mouse_pos = g.IO.MousePos;
    // Log::debug("{}", normalized_value);

    if (ImGui::IsItemActivated()) {
        g.SliderGrabClickOffset = mouse_pos.y - ((1.0f - (float)normalized_value) * scroll_height + cursor_pos.y);
    }

    float inv_normalized_default_value = 0.0f;
    if (held || props.with_default_value_tick)
        inv_normalized_default_value = 1.0f - (float)range.plain_to_normalized(default_value);

    if (held) {
        float current_grab_pos = math::round(mouse_pos.y - cursor_pos.y - g.SliderGrabClickOffset);
        float default_value_grab_pos = math::round(inv_normalized_default_value * scroll_height);
        float val = !math::near_equal(current_grab_pos, default_value_grab_pos) ? current_grab_pos * inv_scroll_height
                                                                                : inv_normalized_default_value;
        normalized_value = std::clamp(1.0f - val, 0.0f, 1.0f);
        *value = (T)range.normalized_to_plain(normalized_value);
    }

    const float half_grab_size_y = grab_size.y * 0.5f;
    const float grab_pos = (1.0f - (float)normalized_value) * scroll_height;
    const float center_x = cursor_pos.x + size.x * 0.5f;
    const ImU32 grab_col = ImGui::GetColorU32(color.Value);
    const ImU32 frame_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
    const ImVec2 frame_rect_min(center_x - frame_width * 0.5f, cursor_pos.y + grab_size.y * 0.5f);
    const ImVec2 frame_rect_max(frame_rect_min.x + frame_width, frame_rect_min.y + scroll_height);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Draw frame
    draw_list->AddRectFilled(frame_rect_min, frame_rect_max, frame_col);

    // Draw default value tick line
    if (props.with_default_value_tick) {
        float default_grab_pos = inv_normalized_default_value * scroll_height + half_grab_size_y;
        default_grab_pos = math::round(default_grab_pos + cursor_pos.y);
        draw_list->AddLine(ImVec2(cursor_pos.x, default_grab_pos), ImVec2(center_x - frame_width, default_grab_pos),
                           frame_col);
        draw_list->AddLine(ImVec2(center_x + frame_width, default_grab_pos), ImVec2(bb.Max.x, default_grab_pos),
                           frame_col);
    }

    // Draw grab
    if (props.grab_shape == SliderGrabShape::Rectangle) {
        constexpr float grab_tick_padding_x = 2.0f;
        const ImVec2 grab_rect_min(center_x - grab_size.x * 0.5f, cursor_pos.y + math::round(grab_pos));
        const ImVec2 grab_rect_max(grab_rect_min.x + grab_size.x, grab_rect_min.y + grab_size.y);
        const ImVec2 grab_tick_min(grab_rect_min.x + grab_tick_padding_x, grab_rect_min.y + grab_size.y * 0.5f);
        const ImVec2 grab_tick_max(grab_rect_min.x + grab_size.x - grab_tick_padding_x, grab_tick_min.y);
        draw_list->AddRectFilled(grab_rect_min, grab_rect_max, grab_col, props.grab_roundness);
        draw_list->AddLine(grab_tick_min, grab_tick_max, 0xFFFFFFFF, 1.0f);
    } else {
        const float radius1 = grab_size.x * 0.5f;
        const float radius2 = grab_size.x * 0.25f;
        const float pos_y = math::round(grab_pos) + radius1;
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius1, grab_col);
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius2, 0xFFFFFFFF);
    }

    if (held) {
        const ImVec2 tooltip_pos(bb.Max.x + 10.0f, cursor_pos.y + math::round(grab_pos));
        ImGui::SetNextWindowPos(tooltip_pos);
        ImGui::BeginTooltip();
        ImGui::Text(format, *value);
        ImGui::EndTooltip();
    }

    if (dragging) {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta();
        if (delta.y != 0.0f)
            return true;
    }

    return false;
}

template <typename T>
static bool knob(const KnobProperties& props, const char* str_id, const ImVec2& size, T* value) {
    ImGuiWindow* window = GImGui->CurrentWindow;
    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(ImVec2(pos.x, pos.y), ImVec2(pos.x, pos.y) + size);
    ImGuiID id = ImGui::GetID(str_id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool dragging = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);

    if (ImGui::IsItemActivated()) {
        ImVec2 pos = ImGui::GetMousePos();
        GImGui->ColorPickerRef.x = pos.x;
        GImGui->ColorPickerRef.y = pos.y;
        // Reset relative mouse state to prevent jumping
        set_mouse_pos((int)pos.x, (int)pos.y);
        reset_relative_mouse_state();
        enable_relative_mouse_mode(true);
    }

    if (ImGui::IsItemDeactivated()) {
        // Reset mouse position back to its original click position
        enable_relative_mouse_mode(false);
        set_mouse_pos((int)GImGui->ColorPickerRef.x, (int)GImGui->ColorPickerRef.y);
    }

    static constexpr double two_pi = 2.0 * std::numbers::pi;
    static constexpr float half_pi = 0.5f * std::numbers::pi_v<float>;
    static constexpr float pointer_offset = 2.0f;
    const float radius = math::min(size.x, size.y) * 0.5f;
    const float body_radius = radius * props.body_size;
    double current_value = (double)*value;

    if (held) {
        int x, y;
        get_relative_mouse_state(&x, &y);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        if (y != 0.0f) {
            const double circumference = (double)radius * two_pi;
            const double inc = (double)y / circumference;
            current_value = math::clamp(current_value - inc * 0.5, 0.0, 1.0);
            *value = (T)current_value;
            dragging = true;
        }
    }

    const float angle = (float)(math::lerp((float)current_value, props.min_angle, props.max_angle) + half_pi);
    const float dir_x = std::cos(angle);
    const float dir_y = std::sin(angle);
    const float min_radius = body_radius * props.pointer_min_len;
    const float max_radius = body_radius * props.pointer_max_len;
    const float dist_angle = props.max_angle - props.min_angle;
    const ImVec2 center = pos + size * ImVec2(0.5f, 0.5f);
    ImDrawList* dl = window->DrawList;

    if (props.body_size < 1.0f) {
        const float min_angle = half_pi + props.min_angle;
        const float max_angle = half_pi + props.max_angle;
        const bool partial_arc = dist_angle < (float)two_pi;
        
        // Draw arc background
        if (partial_arc) {
            dl->PathLineTo(center);
            dl->PathArcTo(center, radius, min_angle, max_angle);
            dl->PathFillConcave(props.arc_bg_color);
        } else {
            dl->AddCircleFilled(center, radius, props.arc_bg_color);
        }

        // Draw indicator arc
        if (current_value > 0.0) {
            if (current_value < 1.0 || partial_arc) {
                float dist = angle - min_angle;
                dl->PathLineTo(center);
                dl->PathArcTo(center, radius, min_angle, angle, (int)(dist * body_radius * 0.5f));
                dl->PathFillConcave(props.arc_color);
            } else {
                dl->AddCircleFilled(center, radius, props.arc_color);
            }
        }
    }

    // Draw body and pointer
    const ImU32 body_color = !(hovered || held || dragging) ? props.body_color : props.body_color + 0x00101010;
    dl->AddCircleFilled(center, body_radius, body_color);
    dl->AddLine(center + ImVec2(dir_x * min_radius, dir_y * min_radius),
                center + ImVec2(dir_x * max_radius, dir_y * max_radius), props.pointer_color, 3.0f);

    //dl->AddRect(bb.Min, bb.Max, 0xFFFFFFFF);

    return dragging;
}

bool begin_window(const char* title, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);
void end_window();
void song_position();
bool toggle_button();
bool param_drag_db(const char* str_id, float* value, float speed = 0.1f, float min_db = -72.0f, float max_db = 6.0f,
                   const char* format = "%.2fdB", ImGuiSliderFlags flags = ImGuiSliderFlags_Vertical);
bool param_drag_panning(const char* str_id, float* value, float speed = 1.0f,
                        ImGuiSliderFlags flags = ImGuiSliderFlags_Vertical);
bool param_slider_db(const SliderProperties& properties, const char* str_id, const ImVec2& size, const ImColor& color,
                     float* value, const NonLinearRange& db_range, float default_value = 0.0f);
bool mixer_label(const char* caption, const float height, const ImColor& color);
void level_meter_options();
void level_meter(const char* str_id, const ImVec2& size, uint32_t count, VUMeter* channels,
                 LevelMeterColorMode color_mode, bool border = true);
void render_test_controls();
extern bool g_test_control_shown;

} // namespace wb::controls