#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include "core/vector.h"

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
  uint32_t color_{};
  uint32_t vtx_offset_{};
  uint32_t old_vtx_offset_{};

  DrawCommandList() {
    reset();
  }

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

inline void im_draw_rect_filled(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float rounding = 0.0f) {
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, rounding);
}

inline void im_draw_rect(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float rounding = 0.0f) {
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, rounding);
}

inline void im_draw_box_filled(ImDrawList* dl, float x, float y, float w, float h, ImU32 col, float rounding = 0.0f) {
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col, rounding);
}

inline void im_draw_box(ImDrawList* dl, float x, float y, float w, float h, ImU32 col, float rounding = 0.0f) {
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col, rounding);
}

inline void im_draw_hline(ImDrawList* dl, float y, float x0, float x1, ImU32 col, float thickness = 1.0f) {
  dl->AddLine({ x0, y }, { x1, y }, col, thickness);
}

inline void im_draw_vline(ImDrawList* dl, float x, float y0, float y1, ImU32 col, float thickness = 1.0f) {
  dl->AddLine({ x, y0 }, { x, y1 }, col, thickness);
}

inline void im_draw_simple_quad(
    ImDrawList* dl,
    const ImVec2& a,
    const ImVec2& b,
    const ImVec2& c,
    const ImVec2& d,
    const ImVec2& uv,
    ImU32 col) {
  dl->PrimReserve(6, 4);
  ImDrawVert* vtx_write_ptr = dl->_VtxWritePtr;
  uint32_t* idx_write_ptr = dl->_IdxWritePtr;
  ImDrawIdx idx = dl->_VtxCurrentIdx;
  idx_write_ptr[0] = idx;
  idx_write_ptr[1] = (ImDrawIdx)(idx + 1);
  idx_write_ptr[2] = (ImDrawIdx)(idx + 2);
  idx_write_ptr[3] = idx;
  idx_write_ptr[4] = (ImDrawIdx)(idx + 2);
  idx_write_ptr[5] = (ImDrawIdx)(idx + 3);
  vtx_write_ptr[0].pos = a;
  vtx_write_ptr[0].uv = uv;
  vtx_write_ptr[0].col = col;
  vtx_write_ptr[1].pos = b;
  vtx_write_ptr[1].uv = uv;
  vtx_write_ptr[1].col = col;
  vtx_write_ptr[2].pos = c;
  vtx_write_ptr[2].uv = uv;
  vtx_write_ptr[2].col = col;
  vtx_write_ptr[3].pos = d;
  vtx_write_ptr[3].uv = uv;
  vtx_write_ptr[3].col = col;
  dl->_VtxCurrentIdx += 4;
  dl->_VtxWritePtr += 4;
  dl->_IdxWritePtr += 6;
}

ImVec2 im_draw_simple_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImU32 text_color);
void im_draw_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect, ImU32 text_color);
void im_draw_line_segment(ImDrawList* draw_list, const ImVec2& p0, const ImVec2& p1, ImU32 col, float thickness);

}  // namespace wb