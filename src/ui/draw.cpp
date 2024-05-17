#include "draw.h"

namespace wb {

ImVec2 draw_monospace_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImU32 text_color) {
    ImFont* font = ImGui::GetFont();
    float half_size = font->FontSize * 0.5f;
    float x = (float)(int)pos.x;
    float y = (float)(int)pos.y;
    const ImFontGlyph* glyph;
    // draw_list->AddText(pos, text_color, text);
    char c;
    while ((c = *text++)) {
        glyph = font->FindGlyph(c);
        if (!glyph)
            continue;
        if (glyph->Visible) {
            draw_list->PrimReserve(6, 4);
            draw_list->PrimRectUV(
                ImVec2(x + glyph->X0, y + glyph->Y0), ImVec2(x + glyph->X1, y + glyph->Y1),
                ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1), text_color);
        }
        x += half_size;
    }
    return pos;
}

void draw_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect,
                        ImU32 text_color) {
    pos.x = IM_ROUND(pos.x);
    pos.y = IM_ROUND(pos.y);
    ImFont* font = GImGui->Font;
    const ImFontGlyph* glyph;
    char c;
    ImGuiContext& g = *GImGui;
    ImVec2 text_size = ImGui::CalcTextSize(text);
    while ((c = *text++)) {
        glyph = font->FindGlyph(c);
        if (!glyph)
            continue;
        DrawList->PrimReserve(6, 4);
        DrawList->PrimQuadUV(
            pos + ImVec2(glyph->Y0, -glyph->X0), pos + ImVec2(glyph->Y0, -glyph->X1),
            pos + ImVec2(glyph->Y1, -glyph->X1), pos + ImVec2(glyph->Y1, -glyph->X0),
            ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
            ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1), text_color);
        pos.y -= glyph->AdvanceX;
    }
}

} // namespace wb