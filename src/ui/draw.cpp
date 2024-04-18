#include "draw.h"

namespace wb {

void add_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect, ImU32 text_color) {
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