#pragma once

#include <imgui.h>

#include "engine/track.h"
#include "gfx/renderer.h"
#include "plughost/plugin_interface.h"
#include "timeline_base.h"

namespace wb {

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

enum class ClipSelectStatus {
  NotSelected,
  Selected,
  PartiallySelected,
};

struct ClipDrawCmd {
  ClipType type;
  ClipHover hover_state;
  Clip* clip;
  double start_offset;
  double min_pos_x;
  double max_pos_x;
  float min_pos_y;
  float height;
  float gain;
  bool layer2;
  union {
    WaveformVisual* audio;
    MidiData* midi;
  };
};

struct SelectedClipRange {
  uint32_t track_id;
  float track_y;
  bool has_clip_selected;
  ClipQueryResult range;

  bool right_side_partially_selected(uint32_t id) const {
    return range.first == id && range.first_offset > 0.0;
  }

  bool left_side_partially_selected(uint32_t id) const {
    return range.last == id && range.last_offset < 0.0;
  }

  ClipSelectStatus is_clip_selected(uint32_t id) {
    if (math::in_range(id, range.first, range.last)) {
      if (id == range.first && range.first_offset > 0.0) {
        return ClipSelectStatus::PartiallySelected;
      }
      if (id == range.last && range.last_offset < 0.0) {
        return ClipSelectStatus::PartiallySelected;
      }
      return ClipSelectStatus::Selected;
    }
    return ClipSelectStatus::NotSelected;
  }
};

struct ClipResizeInfo {
  bool should_resize;
  uint32_t clip_id;
};

struct GuiTimeline : public TimelineBase {
  static constexpr uint32_t highlight_color = 0x9F555555;
  static constexpr float track_separator_height = 2.0f;

  double beat_duration;

  ImU32 text_color{};
  ImU32 text_transparent_color{};
  ImU32 splitter_color{};
  ImU32 splitter_hover_color{};
  ImU32 splitter_active_color{};

  GPUTexture* timeline_fb{};
  ImDrawList* main_draw_list;
  ImDrawList* layer1_draw_list;
  ImDrawList* layer2_draw_list;
  ImDrawList* layer3_draw_list;
  ImDrawData layer_draw_data;
  Vector<ClipDrawCmd> clip_draw_cmd;
  Vector<WaveformDrawCmd> waveform_cmd_list1;
  Vector<WaveformDrawCmd> waveform_cmd_list2;

  ImFont* font;
  uint32_t color_spin = 0;
  uint32_t initial_track_id = 0;
  float mouse_wheel = 0.0f;
  float mouse_wheel_h = 0.0f;
  float font_size = 0.0f;
  float vsplitter_default_size = 150.0f;
  float vscroll = 0.0f;
  float last_vscroll = 0.0f;
  float scroll_delta_y = 0.0f;
  float timeline_scroll_offset_x_f32;
  float timeline_bounds_min_x;
  float timeline_bounds_min_y;
  float timeline_bounds_max_x;
  float current_value = 0.0f;
  double timeline_scroll_offset_x;
  double clip_scale = 0.0;
  double initial_time_pos = 0.0;
  ImVec2 mouse_pos;
  ImVec2 content_min;
  ImVec2 content_max;
  ImVec2 content_size;
  ImVec2 timeline_view_pos;
  ImVec2 old_timeline_size;

  Vector<SelectedClipRange> selected_clip_ranges;
  uint32_t first_selected_track = 0;
  uint32_t last_selected_track = 0;
  float first_selected_track_pos_y = 0.0;
  double selection_start_pos;
  double selection_end_pos;

  bool force_redraw = false;
  bool has_deleted_clips = false;
  bool selecting_range = false;
  bool range_selected = false;
  bool scrolling = false;
  bool timeline_window_focused = false;
  bool edit_selected = false;
  bool left_mouse_clicked = false;
  bool left_mouse_down = false;
  bool middle_mouse_clicked = false;
  bool middle_mouse_down = false;
  bool right_mouse_clicked = false;
  bool holding_shift = false;
  bool holding_ctrl = false;
  bool holding_alt = false;

  TimelineEditAction edit_action{};
  Track* edited_track{};
  Clip* edited_clip{};
  std::optional<int32_t> edit_src_track_id{};
  float edited_track_pos_y;
  Vector<ClipResizeInfo> clip_resize;

  Track* hovered_track{};
  std::optional<int32_t> hovered_track_id{};
  float hovered_track_y = 0.0f;
  float hovered_track_height = 60.0f;

  Track* context_menu_track{};
  uint32_t context_menu_track_id{};
  Clip* context_menu_clip{};
  ImColor tmp_color;
  std::string tmp_name;

  void init();
  void shutdown();
  void reset();
  void render();
  void render_splitter();
  void render_track_controls();
  void render_track_lanes();
  void render_track(
      Track* track,
      uint32_t id,
      float track_pos_y,
      double mouse_at_gridline,
      bool track_hovered,
      bool is_mouse_in_selection_range);
  void render_edited_clips(double mouse_at_gridline);
  void render_clip(Clip* clip, double min_time, double max_time, double start_offset, float track_pos_y, float height, bool layer2 = false);
  void draw_clips(const Vector<ClipDrawCmd>& clip_cmd_list, double sample_scale, float offset_y);
  void query_selected_range();
  bool prepare_resize_for_selected_range(Clip* clip, bool dir);
  float get_track_position_y(uint32_t id);
  void recalculate_song_length();
  void finish_edit();
  void add_track();
  void add_plugin(Track* track, PluginUID uid);

  inline void redraw_screen() {
    force_redraw = true;
  }
};

extern GuiTimeline g_timeline;
}  // namespace wb