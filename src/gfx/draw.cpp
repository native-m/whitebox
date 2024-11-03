#include "draw.h"
#include <limits>
#include <cmath>

namespace wb {

void DrawCommandList::reset() {
    reset_fill_rect();
    vtx_buffer_.resize(0);
}

void DrawCommandList::set_clip_rect(const ImRect& rect) {
    clip_rect_ = rect;
}

void DrawCommandList::set_color(const uint32_t color) {
    color_ = color;
}

void DrawCommandList::add_rect_filled(const ImRect& rect) {
    push_point(rect.Min.x, rect.Min.y);
    push_point(rect.Max.x, rect.Min.y);
    push_point(rect.Max.x, rect.Max.y);
    push_point(rect.Min.x, rect.Max.y);
    push_point(rect.Min.x, rect.Min.y);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Rasterize,
        .rasterize =
            {
                .fill_rect = fill_rect_,
                .vtx_offset = vtx_offset_,
                .vtx_count = 5,
            },
    });
    vtx_offset_ += 5;
}

void DrawCommandList::add_triangle_filled(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2) {
    push_point(p0.x, p0.y);
    push_point(p1.x, p1.y);
    push_point(p2.x, p2.y);
    push_point(p0.x, p0.y);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Rasterize,
        .rasterize =
            {
                .fill_rect = fill_rect_,
                .vtx_offset = vtx_offset_,
                .vtx_count = 4,
            },
    });
    vtx_offset_ += 4;
}

void DrawCommandList::add_polygon(const ImVec2* points, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const ImVec2& point = points[i];
        push_point(point.x, point.y);
    }
    push_point(points[0].x, points[0].y);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Rasterize,
        .rasterize =
            {
                .fill_rect = fill_rect_,
                .vtx_offset = vtx_offset_,
                .vtx_count = count + 1u,
            },
    });
    vtx_offset_ += count + 1u;
}

void DrawCommandList::draw_rect_filled(const ImRect& rect) {
    add_rect_filled(rect);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Fill,
        .fill =
            {
                .fill_rect = fill_rect_,
                .color = color_,
            },
    });
    reset_fill_rect();
}

void DrawCommandList::draw_triangle_filled(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2) {
    add_triangle_filled(p0, p1, p2);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Fill,
        .fill =
            {
                .fill_rect = fill_rect_,
                .color = color_,
            },
    });
    reset_fill_rect();
}

void DrawCommandList::draw_polygon(const ImVec2* points, uint32_t count) {
    add_polygon(points, count);
    commands_.push_back(DrawCommandData {
        .type = DrawCommand::Fill,
        .fill =
            {
                .fill_rect = fill_rect_,
                .color = color_,
            },
    });
    reset_fill_rect();
}

ImVec2 draw_simple_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImU32 text_color) {
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

void draw_vertical_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImVec4 rect,
                        ImU32 text_color) {
    pos.x = IM_ROUND(pos.x);
    pos.y = IM_ROUND(pos.y);
    ImFont* font = GImGui->Font;
    const ImFontGlyph* glyph;
    ImGuiContext& g = *GImGui;
    ImVec2 text_size = ImGui::CalcTextSize(text);
    char c;
    while ((c = *text++)) {
        glyph = font->FindGlyph(c);
        if (!glyph)
            continue;
        if (glyph->Visible) {
            draw_list->PrimReserve(6, 4);
            draw_list->PrimQuadUV(
                pos + ImVec2(glyph->Y0, -glyph->X0), pos + ImVec2(glyph->Y0, -glyph->X1),
                pos + ImVec2(glyph->Y1, -glyph->X1), pos + ImVec2(glyph->Y1, -glyph->X0),
                ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
                ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1), text_color);
        }
        pos.y -= glyph->AdvanceX;
    }
}

void draw_line_segment(ImDrawList* draw_list, const ImVec2& p0, const ImVec2& p1, ImU32 col, float thickness) {
    const float tx = p1.x - p0.x;
    const float ty = p1.y - p0.y;
    const ImVec2 n(ty, -tx);
    const float inv_length = (thickness * 0.5f) / std::sqrt(n.x * n.x + n.y * n.y);
    const float nx = n.x * inv_length;
    const float ny = n.y * inv_length;
    draw_list->PathLineTo(ImVec2(p0.x + nx, p0.y + ny));
    draw_list->PathLineTo(ImVec2(p1.x + nx, p1.y + ny));
    draw_list->PathLineTo(ImVec2(p1.x - nx, p1.y - ny));
    draw_list->PathLineTo(ImVec2(p0.x - nx, p0.y - ny));
    draw_list->PathFillConvex(col);
}

} // namespace wb