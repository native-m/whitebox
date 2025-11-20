#pragma once

#include "core/common.h"
#include "core/types.h"

namespace wb {

struct TimelineViewState {
  double start = 0.0;
  double end = 1.0;

  void zoom(float acc, float zoom_pos, double song_length, double view_scale);
  bool scroll(float scroll_delta, double song_length, double view_scale);

  inline double get_view_scale(double song_length, float timeline_width) const {
    return ((end - start) * song_length) / (double)timeline_width;
  }
};

}  // namespace wb

namespace wb::controls {

enum class TimelineRulerResult {
  None,
  Zoom,
  TimePositionChanged,
};

bool timeline_scrollbar(const char* str_id, float width, double step_size, double max_length, TimelineViewState* view_range);

TimelineRulerResult timeline_ruler(
    const char* str_id,
    int32_t grid_mode,
    bool triplet,
    float width,
    double song_length,
    double playhead_start,
    double* time_pos,
    TimelineViewState* view_range);

Pair<double, double>
timeline_zoom(float acc, float zoom_pos, double song_length, double view_scale, double start_pos, double end_pos);

Pair<double, double> timeline_scroll(float drag_delta, double max_length, double acc, double start_pos, double end_pos);

inline static double timeline_calc_view_scale(double start_pos, double end_pos, double song_length, float timeline_width) {
  return ((end_pos - start_pos) * song_length) / (double)timeline_width;
}

}  // namespace wb::controls