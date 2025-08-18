#pragma once

#include "core/common.h"

namespace wb::controls {

enum class TimelineRulerResult {
  None,
  Zoom,
  TimePositionChanged,
};

bool timeline_scrollbar(
    const char* str_id,
    float width,
    double step_size,
    double max_length,
    double* start_pos,
    double* end_pos);

TimelineRulerResult
timeline_ruler(const char* str_id, float width, double song_length, double* start_pos, double* end_pos, double* time_pos);

Pair<double, double>
timeline_zoom(float acc, float zoom_pos, double song_length, double view_scale, double start_pos, double end_pos);

inline static double timeline_calc_view_scale(double start_pos, double end_pos, double song_length, float timeline_width) {
  return ((end_pos - start_pos) * song_length) / (double)timeline_width;
}

}  // namespace wb::controls