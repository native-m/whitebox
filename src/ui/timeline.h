#pragma once

#include "core/common.h"
#include "engine/track.h"
#include "gfx/renderer.h"
#include "ui/timeline_base.h"
#include <imgui.h>
#include <memory>

namespace wb {

struct BrowserFilePayload;

enum class TimelineEditAction {
    None,
    ClipMove,
    ClipResizeLeft,
    ClipResizeRight,
    ClipShiftLeft,
    ClipShiftRight,
    ClipShift,
    ClipDuplicate,
    ClipAdjustGain,
    ShowClipContextMenu,
};

struct SelectionRange {
    Track* track;
    double min;
    double max;
};

struct TargetSelectionRange {
    uint32_t first_track;
    uint32_t last_track;
    double min;
    double max;
};

struct SelectedClipRange {
    uint32_t track_id;
    float track_y;
    ClipQueryResult range;
};

struct ClipDrawCmd {
    Clip* clip;
    double min_pos_x;
    double max_pos_x;
    double content_offset;
    float min_pos_y;
    float max_pos_y;
};

struct GuiTimeline : public TimelineBase {
    bool force_redraw = false;
    uint32_t color_spin = 0;
    ImDrawList* main_draw_list {};
    ImDrawList* layer1_draw_list {}; // Clip header & background
    ImDrawList* layer2_draw_list {}; // Clip controls
    ImDrawList* layer3_draw_list {}; // Drag & drop
    ImDrawData layer_draw_data;
    Vector<ClipDrawCmd> clip_draw_cmd;
    Vector<WaveformDrawCmd> clip_content_cmds;
    GPUTexture* timeline_fb {};

    ImVec2 window_origin;
    ImVec2 timeline_view_pos;
    ImVec2 content_min;
    ImVec2 content_max;
    ImVec2 area_size;
    ImVec2 old_timeline_size;
    ImVec2 last_mouse_pos;

    double initial_time_pos = 0.0;
    float current_value = 0.0f;
    uint32_t initial_track_id = 0;
    float vscroll = 0.0f;
    float last_vscroll = 0.0f;
    float scroll_delta_y = 0.0f;

    double beat_duration = 0.0;
    double clip_scale = 0.0;
    double sample_scale = 0.0;
    double timeline_scroll_offset_x = 0.0;
    float timeline_scroll_offset_x_f32 = 0.0;
    bool has_deleted_clips = false;
    bool dragging_file = false;
    bool item_dropped = false;
    BrowserFilePayload* drop_payload_data = {};

    TargetSelectionRange target_sel_range;
    Vector<SelectedClipRange> selected_clips;
    TimelineEditAction edit_action;
    bool scrolling = false;
    bool zooming = false;
    bool selecting_range = false;
    bool range_selected = false;

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
    std::optional<uint32_t> context_menu_track_id {};
    Track* context_menu_track {};
    Clip* context_menu_clip {};
    ImColor tmp_color;
    std::string tmp_name;

    static constexpr uint32_t playhead_color = 0xE553A3F9;

    void init();
    void shutdown();
    void add_track();
    void reset();
    void render();
    void render_separator();
    void render_track_controls();
    void clip_context_menu();
    void render_track_lanes();
    void render_track(Track* track, uint32_t id, bool hovering_track_rect, bool hovering_current_track,
                      bool left_clicked, bool right_clicked, float offset_y, float track_pos_y, float next_pos_y,
                      float timeline_end_x, double mouse_at_gridline);
    void select_range();
    void finish_edit_action();
    void recalculate_timeline_length();

    void draw_clip(const Clip* clip, float timeline_width, float offset_y, float min_draw_x, double min_x, double max_x,
                   double clip_scale, double sample_scale, double start_offset, float track_pos_y, float track_height,
                   float gain, const ImColor& track_color, const ImColor& text_color, ImFont* font);

    void draw_clip_overlay(ImVec2 pos, float size, float alpha, const ImColor& col, const char* caption);

    inline void redraw_screen() { force_redraw = true; }
};

extern GuiTimeline g_timeline;
} // namespace wb