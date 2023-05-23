#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "global_state.h"

namespace wb
{
    struct ClipInfo
    {
        uint32_t color;
        double min_time;
        double max_time;
    };

    enum class GUITimelineClipAction
    {
        None,
        Move,
        ResizeLeft,
        ResizeRight
    };

    struct GUITimeline
    {
        float separator_x = 150.f;
        double music_length = 10000.0f;
        float timeline_width = 1.0f;
        double min_scroll_pos_x = 0.0;
        double max_scroll_pos_x = 1.0;
        double last_min_scroll_pos_x = 0.0;
        float scroll_delta_y = 0.0f;
        float last_scroll_pos_y = 0.0f;
        double playhead_position{};
        bool resizing_lhs_scroll_grab = false;
        bool resizing_rhs_scroll_grab = false;
        bool grabbing_scroll = false;
        bool scrolling = false;
        bool zooming = false;
        bool docked = false;
        uint32_t current_clip_n = 0;

        GUITimelineClipAction clip_action{};
        double initial_move_pos = 0.0f;

        static constexpr uint32_t playhead_color = 0xFF53A3F9;

        GUITimeline();
        void render_track_header(Track& track);
        void render_track_context_menu(Track& track);
        void render_track_controls(Track& track);
        void render_horizontal_scrollbar();
        void render_time_ruler();
        void render();
        void handle_scroll_drag_x(float drag_delta, double scroll_view_width, float direction = 1.0f);
        void handle_zoom(float mouse_pos_x, float cursor_pos_x, float view_scale, float mouse_wheel);
        float get_playhead_screen_position(float view_scale, double playhead_position);
        float calculate_music_length();
    };

    extern GUITimeline g_gui_timeline;
}