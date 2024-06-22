#include "controls.h"
#include "core/debug.h"
#include "core/math.h"
#include "core/queue.h"
#include "draw.h"
#include "engine/engine.h"
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

bool param_drag_db(const char* str_id, float* value, float speed, float min_db, float max_db,
                   const char* format, ImGuiSliderFlags flags) {
    char tmp[16] {};
    const char* str_value = tmp;
    flags |= ImGuiSliderFlags_AlwaysClamp; // | ImGuiSliderFlags_Logarithmic |
                                           // ImGuiSliderFlags_NoRoundToFormat;

    if (*value > 0.0f) {
        float db_value = math::linear_to_db(*value);
        fmt::format_to(tmp, "{:.2f}db", db_value);
    } else {
        str_value = "-INFdB";
    }

    return ImGui::DragFloat(str_id, value, 0.005f, 0.0f, 1.0f, str_value, flags);
}

bool param_slider_db(AudioParameterList& param_list, uint32_t id,
                     const SliderProperties& properties, const char* str_id, const ImVec2& size,
                     const ImColor& color, float min_db, float max_db, const char* format) {
    float value = param_list.get_plain_float(id);

    if (value == min_db) {
        format = "-INFdB";
    }

    if (slider2<float>(properties, str_id, size, color, &value, min_db, max_db)) {
        if (value == min_db) {
            param_list.set(id, 0.0f, min_db);
        } else {
            param_list.set(id, math::db_to_linear(value), value);
        }
        return true;
    }

    return false;
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

void vu_meter(const char* str_id, const ImVec2& size, uint32_t count, VUMeter* channels,
              bool border) {
    static const ImU32 channel_color = ImColor(121, 166, 91);
    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImVec2 end_pos = start_pos + size;
    ImRect bb(start_pos, end_pos);
    float inner_start_y = start_pos.y + 1.0f;
    float inner_end_y = end_pos.y - 1.0f;
    float inner_height = inner_end_y - inner_start_y;
    float channel_size = size.x / (float)count;
    auto draw_list = GImGui->CurrentWindow->DrawList;
    auto border_col = ImGui::GetColorU32(ImGuiCol_Border);
    ImGuiID id = ImGui::GetID(str_id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return;

    if (border) {
        draw_list->AddRect(start_pos, end_pos, border_col);
    }

    float pos_x = start_pos.x;
    for (uint32_t i = 0; i < count; i++) {
        float channel_pos_x = pos_x;
        pos_x += channel_size;

        float level_height = (1.0 - channels[i].get_value()) * inner_height;
        draw_list->AddRectFilled(ImVec2(channel_pos_x + 1.0f, level_height + start_pos.y + 1.0f),
                                 ImVec2(pos_x - 1.0f, end_pos.y - 1.0f), channel_color);
    }
}

} // namespace wb::controls