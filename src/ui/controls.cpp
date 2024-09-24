#include "controls.h"
#include "core/color.h"
#include "core/debug.h"
#include "core/core_math.h"
#include "core/queue.h"
#include "engine/engine.h"
#include "gfx/draw.h"
#include <fmt/format.h>

namespace wb::controls {

void song_position() {
    float font_size = 13.0f;
    ImFont* font = ImGui::GetFont();
    ImVec2 padding = GImGui->Style.FramePadding;
    ImVec2 position = ImGui::GetCursorScreenPos();
    ImVec2 size(120.0f + padding.x * 2.0f, font_size + padding.y * 2.0f);
    ImRect bb(position, position + size);
    ImGuiID id = ImGui::GetID("##song_position");
    ImDrawList* draw_list = GImGui->CurrentWindow->DrawList;
    uint32_t text_color = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))
        return;
    double playhead = g_engine.playhead_pos();
    float bar = IM_TRUNC(playhead * 0.25) + 1.0f;
    float beat = IM_TRUNC(std::fmod(playhead, 4.0)) + 1.0f;
    float tick = IM_TRUNC(math::fract(playhead) * g_engine.ppq);
    char buf[32] {};
    fmt::format_to(buf, "{}:{}:{:03}", bar, beat, tick);
    ImVec2 text_size = ImGui::CalcTextSize(buf);
    ImVec2 text_pos = position + size * 0.5f - text_size * 0.5f;
    float half_font_size = font->FontSize * 0.5f;
    draw_list->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Button), 2.0f);
    // draw_simple_text(draw_list, "999:00:00", position + size * 0.5f - text_size * 0.5f,
    // text_color);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), buf);
}

template <typename T>
static bool slider2_ranged(const SliderProperties& properties, const char* str_id,
                           const ImVec2& size, const ImColor& color, T* value,
                           const NonLinearRange& db_range, T default_value = 0.0f,
                           const char* format = "%.2f") {
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

    T normalized_value = (T)db_range.plain_to_normalized((float)*value);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    float scroll_height = size.y - grab_size.y;
    float inv_scroll_height = 1.0f / scroll_height;
    const ImVec2& mouse_pos = g.IO.MousePos;
    // Log::debug("{}", normalized_value);

    if (ImGui::IsItemActivated()) {
        g.SliderGrabClickOffset =
            mouse_pos.y - ((1.0f - (float)normalized_value) * scroll_height + cursor_pos.y);
    }

    float inv_normalized_default_value = 0.0f;
    if (held || properties.with_default_value_tick)
        inv_normalized_default_value = 1.0f - (float)db_range.plain_to_normalized(default_value);

    if (held) {
        float current_grab_pos = math::round(mouse_pos.y - cursor_pos.y - g.SliderGrabClickOffset);
        float default_value_grab_pos = math::round(inv_normalized_default_value * scroll_height);
        float val = !math::near_equal(current_grab_pos, default_value_grab_pos)
                        ? current_grab_pos * inv_scroll_height
                        : inv_normalized_default_value;
        normalized_value = std::clamp(1.0f - val, 0.0f, 1.0f);
        *value = (T)db_range.normalized_to_plain(normalized_value);
    }

    float half_grab_size_y = grab_size.y * 0.5f;
    float grab_pos = (1.0f - (float)normalized_value) * scroll_height;
    float center_x = cursor_pos.x + size.x * 0.5f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 frame_rect_min(center_x - frame_width * 0.5f, cursor_pos.y + grab_size.y * 0.5f);
    ImVec2 frame_rect_max(frame_rect_min.x + frame_width, frame_rect_min.y + scroll_height);
    ImU32 frame_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImU32 grab_col = ImGui::GetColorU32(color.Value);

    // draw_list->AddRect(cursor_pos, bb.Max, frame_col);
    // Draw frame
    draw_list->AddRectFilled(frame_rect_min, frame_rect_max, frame_col);

    // Draw default value tick line
    if (properties.with_default_value_tick) {
        float default_grab_pos = inv_normalized_default_value * scroll_height + half_grab_size_y;
        default_grab_pos = math::round(default_grab_pos + cursor_pos.y);
        draw_list->AddLine(ImVec2(cursor_pos.x, default_grab_pos),
                           ImVec2(center_x - frame_width, default_grab_pos),
                           frame_col);
        draw_list->AddLine(ImVec2(center_x + frame_width, default_grab_pos),
                           ImVec2(bb.Max.x, default_grab_pos),
                           frame_col);
    }

    // Draw grab
    if (properties.grab_shape == SliderGrabShape::Rectangle) {
        constexpr float grab_tick_padding_x = 2.0f;
        ImVec2 grab_rect_min(center_x - grab_size.x * 0.5f, cursor_pos.y + math::round(grab_pos));
        ImVec2 grab_rect_max(grab_rect_min.x + grab_size.x, grab_rect_min.y + grab_size.y);
        draw_list->AddRectFilled(grab_rect_min, grab_rect_max, grab_col, properties.grab_roundness);
        ImVec2 grab_tick_min(grab_rect_min.x + grab_tick_padding_x,
                             grab_rect_min.y + grab_size.y * 0.5f);
        ImVec2 grab_tick_max(grab_rect_min.x + grab_size.x - grab_tick_padding_x, grab_tick_min.y);
        draw_list->AddLine(grab_tick_min, grab_tick_max, 0xFFFFFFFF, 1.0f);
    } else {
        float radius1 = grab_size.x * 0.5f;
        float radius2 = grab_size.x * 0.25f;
        float pos_y = math::round(grab_pos) + radius1;
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius1, grab_col);
        draw_list->AddCircleFilled(ImVec2(center_x, cursor_pos.y + pos_y), radius2, 0xFFFFFFFF);
    }

    if (held) {
        ImVec2 tooltip_pos(bb.Max.x + 10.0f, cursor_pos.y + math::round(grab_pos));
        ImGui::SetNextWindowPos(tooltip_pos);
        ImGui::BeginTooltip();
        ImGui::Text(format, *value);
        ImGui::EndTooltip();
    }

    if (dragging) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta();
        if (delta.y != 0.0f)
            return true;
    }

    return false;
}

bool param_drag_db(const char* str_id, float* value, float speed, float min_db, float max_db,
                   const char* format, ImGuiSliderFlags flags) {
    char tmp[16] {};
    const char* str_value = tmp;
    flags |= ImGuiSliderFlags_AlwaysClamp;

    if (*value > min_db) {
        str_value = "%.2fdb";
    } else {
        str_value = "-INFdB";
    }

    return ImGui::DragFloat(str_id, value, 0.1f, min_db, max_db, str_value, flags);
}

bool param_slider_db(const SliderProperties& properties, const char* str_id, const ImVec2& size,
                     const ImColor& color, float* value, const NonLinearRange& db_range,
                     float default_value) {
    const char* format = *value > db_range.min_val ? "%.3fdb" : "-INFdb";
    return slider2_ranged<float>(properties, str_id, size, color, value, db_range, default_value,
                                 format);
}

bool mixer_label(const char* caption, const float height, const ImColor& color) {
    float font_size = GImGui->FontSize;
    ImVec2 padding = GImGui->Style.FramePadding;
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImRect bb(cursor_pos, ImVec2(cursor_pos.x + font_size + 4.0f, cursor_pos.y + height));
    ImGuiID id = ImGui::GetID("##mixer_lbl");

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    auto draw_list = GImGui->CurrentWindow->DrawList;
    draw_list->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg));
    draw_vertical_text(draw_list, caption, ImVec2(bb.Min.x + 2.0f, bb.Max.y - 4.0f), ImVec4(),
                       ImGui::GetColorU32(ImGuiCol_Text));

    return true;
}

struct VUMeterRange {
    float max;
    float min;
    ImU32 color;
};

static constexpr float min_vu_db = -45.0f;
static constexpr float max_vu_db = 6.0f;
static VUMeterRange vu_ranges[] = {
    {
        math::normalize_value(-12.0f, min_vu_db, max_vu_db),
        math::normalize_value(-45.0f, min_vu_db, max_vu_db),
        ImColor(105, 221, 56),
    },
    {
        math::normalize_value(0.0f, min_vu_db, max_vu_db),
        math::normalize_value(-12.0f, min_vu_db, max_vu_db),
        ImColor(195, 255, 70),
    },
    {
        math::normalize_value(6.0f, min_vu_db, max_vu_db),
        math::normalize_value(0.0f, min_vu_db, max_vu_db),
        ImColor(255, 83, 79),
    },
};

void level_meter_options() {
    int i = 0;
    for (auto& range : vu_ranges) {
        ImGui::PushID(i);
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(range.color);
        ImGui::Text("Color %i", i);
        if (ImGui::ColorEdit3("Color", (float*)&col)) {
            range.color = ImGui::ColorConvertFloat4ToU32(col);
        }
        ImGui::PopID();
        i++;
    }
}

void level_meter(const char* str_id, const ImVec2& size, uint32_t count, VUMeter* channels,
                 LevelMeterColorMode color_mode, bool border) {
    // static const ImU32 channel_color = ImColor(121, 166, 91);
    static constexpr float min_db = -45.0f;
    static constexpr float max_db = 6.0f;
    static const float min_amplitude = math::db_to_linear(min_db);
    static const float max_amplitude = math::db_to_linear(max_db);

    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImVec2 end_pos = start_pos + size;
    ImRect bb(start_pos, end_pos);
    float inner_start_y = start_pos.y + 1.0f;
    float inner_end_y = end_pos.y - 1.0f;
    float inner_height = inner_end_y - inner_start_y;
    float channel_size = size.x / (float)count;
    auto draw_list = GImGui->CurrentWindow->DrawList;
    ImU32 border_col;
    ImGuiID id = ImGui::GetID(str_id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return;

    if (border) {
        border_col = ImGui::GetColorU32(ImGuiCol_Border);
        draw_list->AddRect(start_pos, end_pos, border_col);
    } else {
        border_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
    }

    float pos_x = start_pos.x;
    for (uint32_t i = 0; i < count; i++) {
        float level = math::clamp(channels[i].get_value(), min_amplitude, max_amplitude);
        float channel_pos_x = pos_x;
        pos_x += channel_size;

        if (!border) {
            draw_list->AddRectFilled(ImVec2(channel_pos_x + 1.0, start_pos.y + 1.0),
                                     ImVec2(pos_x - 1.0, end_pos.y - 1.0), border_col);
        }

        if (level > min_amplitude) {
            float level_db = math::linear_to_db(level);
            float level_norm = math::normalize_value(level_db, min_db, max_db);
            switch (color_mode) {
                case LevelMeterColorMode::Normal: {
                    for (const auto& range : vu_ranges) {
                        if (level_norm < range.min)
                            break;
                        float level_start = (1.0f - range.min) * inner_height;
                        float level_height =
                            (1.0f - std::min(level_norm, range.max)) * inner_height;
                        draw_list->AddRectFilled(
                            ImVec2(channel_pos_x + 1.0f, level_height + inner_start_y),
                            ImVec2(pos_x - 1.0f, level_start + inner_start_y), range.color);
                    }
                    break;
                }
                case LevelMeterColorMode::Line: {
                    ImU32 color = 0;
                    for (const auto& range : vu_ranges) {
                        if (level_norm <= range.max) {
                            color = range.color;
                            break;
                        }
                    }
                    float level_height = (1.0f - level_norm) * inner_height;
                    draw_list->AddRectFilled(
                        ImVec2(channel_pos_x + 1.0f, level_height + inner_start_y),
                        ImVec2(pos_x - 1.0f, end_pos.y - 1.0f), color);
                    break;
                }
            }
        }
    }
}

} // namespace wb::controls