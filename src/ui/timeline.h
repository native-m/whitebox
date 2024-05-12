#pragma once

#include "core/common.h"
#include "engine/track.h"
#include "renderer.h"
#include <imgui.h>
#include <memory>

namespace wb {

enum class TimelineEditAction {
    None,
    ClipMove,
    ClipResizeLeft,
    ClipResizeRight,
    ClipDuplicate,
    ShowClipContextMenu,
};

struct GuiTimeline {
    bool open = true;
    bool redraw = false;
    bool force_redraw = false;
    uint32_t color_spin = 0;
    ImDrawList* main_draw_list {};
    ImDrawList* priv_draw_list {};
    ImDrawData priv_draw_data;
    std::shared_ptr<Framebuffer> timeline_fb {};
    ImVector<ClipContentDrawCmd> clip_content_cmds;

    ImVec2 window_origin;
    ImVec2 timeline_view_pos;
    ImVec2 content_min;
    ImVec2 content_max;
    ImVec2 area_size;
    ImVec2 old_timeline_size;
    ImVec2 last_mouse_pos;
    float timeline_width = 0.0f;
    float separator_pos = 150.0f;

    double inv_ppq = 0.0;
    double playhead = 0.0;
    double song_length = 10000.0;
    double last_hscroll = 0.0;
    double min_hscroll = 0.0;
    double max_hscroll = 0.074081648971430242;
    double initial_time_pos = 0.0;
    float vscroll = 0.0f;
    float last_vscroll = 0.0f;
    float scroll_delta_y = 0.0f;
    float grid_scale = 2.0f;

    TimelineEditAction edit_action;
    bool scrolling = false;
    bool zooming = false;
    bool zooming_on_ruler = false;
    bool grabbing_scroll = false;
    bool resizing_lhs_scroll_grab = false;
    bool resizing_rhs_scroll_grab = false;

    Track* edited_track {};
    Clip* edited_clip {};
    float edited_track_pos_y;
    double edited_clip_min_time;
    double edited_clip_max_time;

    Track* hovered_track = nullptr;
    float hovered_track_y = 0.0f;
    float hovered_track_height = 60.0f;

    // Context menu stuff...
    Track* context_menu_track {};
    Clip* context_menu_clip {};
    ImColor tmp_color;
    std::string tmp_name;

    static constexpr uint32_t playhead_color = 0xE553A3F9;

    void init();
    void shutdown();
    Track* add_track();
    void render();
    inline void render_horizontal_scrollbar();
    inline void render_time_ruler();
    inline void render_separator();
    inline void render_track_controls();
    inline void track_context_menu(Track& track, int track_id);
    inline void clip_context_menu();
    void render_track_lanes();

    inline void scroll_horizontal(float drag_delta, double max_length, double direction = 1.0);
    inline void zoom(float mouse_pos_x, float cursor_pos_x, double view_scale, float mouse_wheel);

    inline double calc_view_scale() const {
        return (max_hscroll - min_hscroll) * song_length / (double)timeline_width;
    }

    inline void finish_edit_action() {
        hovered_track = nullptr;
        hovered_track_y = 0.0f;
        hovered_track_height = 60.0f;
        edited_clip = nullptr;
        edited_track = nullptr;
        edited_track_pos_y = 0.0f;
        edited_clip_min_time = 0.0;
        edited_clip_max_time = 0.0;
        edit_action = TimelineEditAction::None;
        initial_time_pos = 0.0;
    }

    inline void redraw_screen() { force_redraw = true; }

    inline std::pair<int, int> calc_grid_count() const;
};

extern GuiTimeline g_timeline;
} // namespace wb