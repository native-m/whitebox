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
    double song_length = 10000.0;
    float timeline_width = 0.0f;
    float separator_pos = 150.0f;
    float min_track_control_size = 100.0f;
    float grid_scale = 4.0f;

    bool redraw = false;
    bool zooming_on_ruler = false;
    bool grabbing_scroll = false;
    bool resizing_lhs_scroll_grab = false;
    bool resizing_rhs_scroll_grab = false;

    static constexpr uint32_t playhead_color = 0xE553A3F9;

    void render_horizontal_scrollbar();
    bool render_time_ruler(double* time_value);

    void scroll_horizontal(float drag_delta, double max_length, double direction = 1.0);
    void zoom(float mouse_pos_x, float cursor_pos_x, double view_scale, float mouse_wheel);

    inline double calc_view_scale() const {
        return (max_hscroll - min_hscroll) * song_length / (double)timeline_width;
    }
};
} // namespace wb