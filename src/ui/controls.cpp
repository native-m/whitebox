#include "controls.h"
#include "core/debug.h"
#include "core/math.h"
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

bool param_drag_db(AudioParameterList& param_list, uint32_t id, const char* str_id, float speed,
                   float min_db, float max_db, const char* format, ImGuiSliderFlags flags) {
    float value = param_list.get_plain_float(id);
    flags |= ImGuiSliderFlags_AlwaysClamp;

    if (value == min_db) {
        format = "-INFdB";
    }

    if (ImGui::DragFloat(str_id, &value, speed, min_db, max_db, format, flags)) {
        Log::debug("{}", value);
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

void metering(const ImVec2& size, uint32_t count, const float* channels) {
}

} // namespace wb::controls