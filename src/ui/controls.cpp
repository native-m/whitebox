#include "controls.h"

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

    ImVec2 text_size = ImGui::CalcTextSize("999:00:00");
    ImVec2 text_pos = position + size * 0.5f - text_size * 0.5f;
    float half_font_size = font->FontSize * 0.5f;
    draw_list->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Button), 2.0f);
    draw_monospace_text(draw_list, "999:00:00", text_pos, text_color);
    // draw_list->AddText(position + text_pos, ImGui::GetColorU32(ImGuiCol_Text), "999:00:00");
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