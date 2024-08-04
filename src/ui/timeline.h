#pragma once

#include "core/common.h"
#include "engine/track.h"
#include "gfx/renderer.h"
#include "ui/timeline_base.h"
#include <imgui.h>
#include <memory>

namespace wb {

enum class TimelineEditAction {
    None,
    ClipMove,
    ClipResizeLeft,
    ClipResizeRight,
    ClipShift,
    ClipDuplicate,
    ShowClipContextMenu,
};

struct SelectionRange {
    Track* track;
    double min;
    double max;
};

struct TargetSelectionRange {
    uint32_t start_track;
    uint32_t end_track;
    double min;
    double max;
};

struct GuiTimeline : public TimelineBase {
    bool open = true;
    bool force_redraw = false;
    uint32_t color_spin = 0;
    ImDrawList* main_draw_list {};
    ImDrawList* layer1_draw_list {}; // Clip header & background
    ImDrawList* layer2_draw_list {}; // Clip controls
    ImDrawList* layer3_draw_list {}; // Drag & drop
    ImDrawData layer_draw_data;
    std::shared_ptr<Framebuffer> timeline_fb {};
    ImVector<ClipContentDrawCmd> clip_content_cmds;

    ImVec2 window_origin;
    ImVec2 timeline_view_pos;
    ImVec2 content_min;
    ImVec2 content_max;
    ImVec2 area_size;
    ImVec2 old_timeline_size;
    ImVec2 last_mouse_pos;
    //float timeline_width = 0.0f;
    //float separator_pos = 150.0f;

    /*double inv_ppq = 0.0;
    double playhead = 0.0;
    double song_length = 10000.0;
    double last_hscroll = 0.0;
    double min_hscroll = 0.0;
    double max_hscroll = 0.074081648971430242;*/
    double initial_time_pos = 0.0;
    float vscroll = 0.0f;
    float last_vscroll = 0.0f;
    float scroll_delta_y = 0.0f;
    //float grid_scale = 4.0f;

    Vector<SelectionRange> selection_ranges;
    TargetSelectionRange target_sel_range;
    TimelineEditAction edit_action;
    bool scrolling = false;
    bool zooming = false;
    bool selecting_range = false;
    /*bool zooming_on_ruler = false;
    bool grabbing_scroll = false;
    bool resizing_lhs_scroll_grab = false;
    bool resizing_rhs_scroll_grab = false;*/

    Track* edited_track {};
    Clip* edited_clip {};
    std::optional<uint32_t> edited_track_id {};
    float edited_track_pos_y;
    double edited_clip_min_time;
    double edited_clip_max_time;

    Track* hovered_track {};
    std::optional<uint32_t> hovered_track_id {};
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
    void reset();
    void render();
    inline void render_separator();
    inline void render_track_controls();
    inline void track_context_menu(Track& track, int track_id);
    inline void clip_context_menu();
    void render_track_lanes();
    void finish_edit_action();
    void recalculate_song_length();

    inline void redraw_screen() { force_redraw = true; }

    inline std::pair<int, int> calc_grid_count() const;
};

extern GuiTimeline g_timeline;
} // namespace wb