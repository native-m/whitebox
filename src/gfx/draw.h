#pragma once

#include "core/vector.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace wb {

enum class DrawCommand {
    Rasterize,
    Fill,
};

struct DrawRasterizeCmd {
    ImRect fill_rect;
    uint32_t vtx_offset;
    uint32_t vtx_count;
};

struct DrawFillCmd {
    ImRect fill_rect;
    uint32_t color;
};

struct DrawCommandData {
    DrawCommand type;
    union {
        DrawRasterizeCmd rasterize;
        DrawFillCmd fill;
    };
};

struct EdgeVertex {
    ImVec2 v0, v1;
};

struct DrawCommandList {
    Vector<DrawCommandData> commands_;
    Vector<ImVec2> vtx_buffer_;
    ImRect clip_rect_;
    ImRect fill_rect_;
    uint32_t color_ {};
    uint32_t vtx_offset_ {};
    uint32_t old_vtx_offset_ {};

    DrawCommandList() { reset(); }

    void reset();
    void set_clip_rect(const ImRect& rect);
    void set_color(const uint32_t color);

    void add_rect_filled(const ImRect& rect);
    void add_triangle_filled(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2);
    void add_polygon(const ImVec2* points, uint32_t count);

    void draw_rect_filled(const ImRect& rect);
    void draw_triangle_filled(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2);
    void draw_polygon(const ImVec2* points, uint32_t count);

    void reset_fill_rect() {
        fill_rect_.Min.x = std::numeric_limits<float>::max();
        fill_rect_.Min.y = std::numeric_limits<float>::max();
        fill_rect_.Max.x = std::numeric_limits<float>::min();
        fill_rect_.Max.y = std::numeric_limits<float>::min();
    }

    inline void push_point(float x, float y) {
        if (x < fill_rect_.Min.x)
            fill_rect_.Min.x = x;
        if (y < fill_rect_.Min.y)
            fill_rect_.Min.y = y;
        if (x > fill_rect_.Max.x)
            fill_rect_.Max.x = x;
        if (y > fill_rect_.Max.y)
            fill_rect_.Max.y = y;
        vtx_buffer_.emplace_back(x, y);
    }
};

ImVec2 draw_simple_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImU32 text_color);
void draw_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect,
                        ImU32 text_color);

} // namespace wb