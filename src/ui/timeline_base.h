#pragma once

#include "core/common.h"

namespace wb {
struct TimelineBase {
  double ppq = 0.0;
  double inv_ppq = 0.0;
  double playhead = 0.0;
  double last_hscroll = 0.0;
  double min_hscroll = 0.0;
  double max_hscroll = 1.0;
  double song_length = 100.0;  // 100 beats
  float zoom_position = 0.0;
  float timeline_width = 0.0f;
  float vsplitter_size = 150.0f;
  float vsplitter_min_size = 100.0f;
  float beat_division = 1.0f;

  bool redraw = false;
  bool zooming_on_ruler = false;
  bool grabbing_scroll = false;
  bool resizing_lhs_scroll_grab = false;
  bool resizing_rhs_scroll_grab = false;

  static constexpr float zoom_rate = 0.12f;
  static constexpr uint32_t playhead_color = 0xE553A3F9;

  void render_horizontal_scrollbar();
  bool render_time_ruler(
      double* time_value,
      double playhead_start,
      double sel_start_pos,
      double sel_end_pos,
      bool show_selection_range);

  void scroll_horizontal(float drag_delta, double max_length, double direction = 1.0);
  void zoom(float mouse_pos_x, float cursor_pos_x, double view_scale, float mouse_wheel);

  inline double calc_view_scale() const {
    return ((max_hscroll - min_hscroll) * song_length) / (double)timeline_width;
  }
};
}  // namespace wb