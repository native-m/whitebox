#include "timeline2.h"

#include <imgui.h>

#include <algorithm>

#include "IconsMaterialSymbols.h"
#include "browser.h"
#include "clip_editor.h"
#include "context_menu.h"
#include "controls.h"
#include "core/common.h"
#include "dialogs.h"
#include "engine/clip_command.h"
#include "engine/clip_edit.h"
#include "engine/command_manager2.h"
#include "engine/engine2.h"
#include "engine/etypes.h"
#include "engine/track.h"
#include "engine/track_command.h"
#include "font.h"
#include "gfx/draw.h"
#include "gfx/renderer.h"
#include "grid.h"
#include "hotkeys.h"
#include "plugins.h"
#include "style.h"
#include "timeline_controls.h"

namespace wb {

enum class TimelineTool {
  Move,
  Duplicate,
  Select,
  Draw,
  Slice,
  Shift,
};

enum class TimelineLaneType {
  Track,
  Automation,
};

struct TimelineLane {
  TimelineLaneType type;

  union {
    Track* track;
  };
};

struct TimelineDragDropFilesState {
  BrowserFilePayload* payload_data;
  double position;
  int32_t first_track_id;
  bool item_dropped;
};

struct TimelineToolState {
  int32_t initial_track_id;
  uint32_t clip_id;
  double min_relative_ofs;
  bool left_side;
};

struct TimelineState {
  enum {
    None,
    Select,
    DragDropFiles,
    Move,
    Duplicate,
    Shift,
    Resize,
    Stretch,
    Nudge,
  };

  uint32_t type;
  Vector<ClipSpan> selected_track_clips;
  Vector<TrackClipResizeInfo> resize_clips;
  double initial_pos;

  struct {
    bool is_selected;
    double start_pos;
    double end_pos;
    int32_t first_track_id;
    int32_t last_track_id;
  } select;

  union {
    TimelineDragDropFilesState drag_drop_files;
    TimelineToolState tool;
  };

  bool in_action() const {
    return type != None;
  }

  void clear_selection() {
    if (type == Select)
      type = None;
    selected_track_clips.resize(0);
    select = {};
  }

  void end_action() {
    switch (type) {
      case None:
      case Select: break;
      case DragDropFiles: drag_drop_files = {}; break;
      case Duplicate:
      case Move: tool = {}; break;
      case Resize:
      case Stretch:
      case Nudge:
        resize_clips.resize(0);
        tool = {};
        break;
      default: break;
    }
    initial_pos = 0.0;
    type = None;
  }

  ClipResizeMode get_resize_mode() const {
    switch (type) {
      case Resize: return ClipResizeMode::Resize;
      case Stretch: return ClipResizeMode::Stretch;
      case Nudge: return ClipResizeMode::Nudge;
      default: break;
    }
    return {};
  }
};

struct ClipDrawCmd2 {
  enum {
    Topmost = 1 << 0,
    Highlighted = 1 << 1,
  };

  ClipType type;
  uint32_t flags;
  ColorU32 color;
  double start_offset;
  double start_pos_x;
  double end_pos_x;
  float pos_y;
  float height;
  float gain;
  uint32_t name_len;
  const char* name;

  union {
    struct {
      WaveformVisual* waveform;
      double speed;
    } audio;

    struct {
      MidiData* data;
      int16_t rate;
    } midi;
  };
};

static constexpr uint32_t timeline_mouse_btn_flags_ =
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle;

static constexpr ImGuiWindowFlags track_control_window_flags_ =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_AlwaysUseWindowPadding;

static constexpr float zoom_sensitivity_ = 0.12f;
static constexpr float track_separator_height_ = 2.0f;
static constexpr uint32_t playhead_color_ = 0xE553A3F9;
static constexpr uint32_t highlight_color_ = 0x9F555555;

static TimelineViewState view_state_;
static double view_scale_;
static double scroll_offset_x_;
static double song_duration_ = 100.0;
static float track_panel_width_ = 150.0f;
static float track_panel_min_width_ = 100.0f;
static float scroll_delta_y_ = 0.0f;
static float vscroll_;
static float last_vscroll_;
static float track_lanes_height_;
static float mouse_wheel_;
static float mouse_wheel_h_;
static ImVec2 mouse_pos_;
static ImVec2 view_pos_;
static ImVec2 view_min_;
static ImVec2 view_max_;
static ImVec2 current_fb_scale_;
static ImVec2 timeline_layout_size_;
static ImVec2 timeline_display_size_{ 16.0f, 16.0f };
static ImVec2 track_panel_pos_;
static ImVec2 track_lanes_pos_;
static ImVec2 floating_button_size_;
static int32_t grid_mode_ = 4;
static bool scrolling_;
static bool triplet_;
static bool redraw_ = true;
static bool force_redraw_;
static bool timeline_focused_;
static bool timeline_hovered_;
static bool left_mouse_clicked_;
static bool left_mouse_down_;
static bool middle_mouse_clicked_;
static bool middle_mouse_down_;
static bool right_mouse_clicked_;
static bool right_mouse_down_;
static bool can_select_;
static bool holding_shift_;
static bool holding_ctrl_;
static bool holding_alt_;

static ImU32 text_color_;
static ImU32 text_transparent_color_;
static uint32_t track_color_spin_ = 0;

static GPUTexture* timeline_fb_;
static ImDrawList* main_dl_;
static ImDrawList* layer1_dl_;
static ImDrawList* layer2_dl_;
static ImDrawList* layer3_dl_;
static ImDrawData layer_draw_data_;
static ImFont* font_;
static float font_size_;

static bool stretch_mode_;
static TimelineTool current_tool_;
static float first_visible_track_pos_y_;
static int32_t first_visible_track_;
static int32_t last_visible_track_;
static TimelineState tl_state_;
static std::optional<int32_t> hovered_track_id_;
static std::optional<Pair<int32_t, uint32_t>> selected_clip_;

static bool should_update_track_stack_;
static Vector<float> track_stack_;
static Vector<ClipDrawCmd2> clip_draw_buffer;
static Vector<WaveformDrawCmd> waveform_cmd1;
static Vector<WaveformDrawCmd> waveform_cmd2;

static Track* context_menu_track_{};
static TrackID context_menu_track_id_{};
static Clip* context_menu_clip_{};
static Color tmp_color_;
static std::string tmp_name_;

bool g_timeline2_window_open = true;

static void timeline_render_toolbar();
static void timeline_render_navbar();
static void timeline_render_splitter();
static void timeline_render_track_panel();
static void timeline_render_track_lanes();
static void timeline_render_floating_btns();
static void timeline_render_context_menu();
static void timeline_handle_mouse_event();
static void timeline_handle_key_event();
static void timeline_handle_track_event();
static void timeline_prepare_resize(double resize_pos, bool left);
static void timeline_draw_track_lanes(const ImVec2& display_size, const ImVec2& view_size);
static void timeline_draw_edit_preview();
static void timeline_draw_clips();
static void timeline_update_track_stack(int32_t track_index = 0, int32_t num_tracks = -1);
static void timeline_query_selected_range();

static void timeline_add_midi_clips();
static void timeline_add_clip_from_file();
static bool timeline_delete_region();

inline static double timeline_get_view_scale() {
  return view_state_.get_view_scale(song_duration_, timeline_display_size_.x);
}

inline static double timeline_get_scroll_pos_x() {
  return math::round((view_state_.start * song_duration_) / view_scale_);
}

inline static double timeline_get_hovered_position() {
  double position = ((double)(mouse_pos_.x - view_pos_.x) * view_scale_ + view_state_.start * song_duration_);
  return math::max(round_to_grid(position, 1.0 / view_scale_, grid_mode_, triplet_), 0.0);
}

inline static void timeline_hscroll(float scroll_delta) {
  if (view_state_.scroll(scroll_delta, song_duration_, -view_scale_)) {
    redraw_ = true;
  }
}

inline static bool timeline_add_clip_draw_data(
    Clip* clip,
    double clip_scale,
    double start_pos,
    double end_pos,
    double start_offset,
    double speed,
    float track_pos_y,
    float height,
    uint32_t draw_flags) {
  const double start_pos_x = scroll_offset_x_ + start_pos * clip_scale;
  const double end_pos_x = scroll_offset_x_ + end_pos * clip_scale;
  const float x0 = (float)math::round(start_pos_x);
  const float x1 = (float)math::round(end_pos_x);

  if (x0 >= view_max_.x)
    return false;
  if (x1 < view_min_.x)
    return true;

  ClipDrawCmd2* cmd = clip_draw_buffer.emplace_back_raw();
  cmd->type = clip->type;
  cmd->color = clip->color.to_uint32();
  cmd->flags = draw_flags;
  cmd->start_offset = start_offset;
  cmd->start_pos_x = start_pos_x;
  cmd->end_pos_x = end_pos_x;
  cmd->pos_y = track_pos_y;
  cmd->height = height;
  cmd->name = clip->name.c_str();
  cmd->name_len = (uint32_t)clip->name.size();

  if (clip->is_audio()) {
    cmd->gain = clip->audio.gain;
    cmd->audio.waveform = clip->audio.asset->waveform_visual;
    cmd->audio.speed = speed;
  } else {
    cmd->midi.data = &clip->midi.asset->data;
    cmd->midi.rate = clip->midi.rate;
  }

  return true;
}

inline static void timeline_draw_clip_label(
    ImDrawList* dl,
    std::string_view name,
    bool mini_clip,
    float height,
    const ImVec2& label_min,
    const ImVec2& label_max) {
  static constexpr float label_padding_x = 5.0f;
  const char* str = name.data();
  const size_t str_size = name.size();
  // const float bg_contrast = label_color.luminance();
  const ColorU32 text_col = text_color_;  // bg_contrast > 0.55f ? content_color_u32 : text_color_;
  const float label_padding_y = mini_clip ? (height - font_size_) * 0.5f : ((label_max.y - label_min.y) - font_size_) * 0.5f;
  const ImVec2 label_pos(std::max(label_min.x, view_min_.x) + label_padding_x, label_min.y + label_padding_y);
  const ImVec4 clip_label_rect(label_min.x, label_min.y, label_max.x - 6.0f, label_max.y);
  dl->AddText(font_, font_size_, label_pos, text_col, str, str + str_size, 0.0f, &clip_label_rect);
}

inline static double timeline_get_minimum_move_pos() {
  double min_pos = 0.0;
  for (int32_t i = 0; const auto& clip_span : tl_state_.selected_track_clips) {
    if (!clip_span.contains_clip) {
      i++;
      continue;
    }
    Track* track = Engine2::tracks[i + tl_state_.select.first_track_id];
    Clip* clip = track->clips[clip_span.first];
    double start_pos = clip->min_time + clip_span.first_offset;
    min_pos = math::max(min_pos, start_pos);
    i++;
  }
  return -min_pos;
}

inline static float timeline_get_track_end_pos_y(int32_t track_id) {
  Track* track = Engine2::tracks[track_id];
  return track_stack_[track_id] + track->get_height() + track_separator_height_;
}

void timeline_init() {
  Engine2::add_bpm_update_listener(nullptr, [](void* userdata, double beat_duration, double bpm) { force_redraw_ = true; });
  CommandManager2::add_cmd_history_update_listener(nullptr, [](void* userdata, bool is_undo) {
    double new_song_duration = math::max(Engine2::get_song_duration() + 4.0, 100.0);

    if (is_undo) {
      tl_state_.clear_selection();
    }

    if (new_song_duration != song_duration_) {
      view_state_.start = view_state_.start * song_duration_ / new_song_duration;
      view_state_.end = view_state_.end * song_duration_ / new_song_duration;
      song_duration_ = new_song_duration;
    }

    if (track_stack_.size() != Engine2::tracks.size()) {
      should_update_track_stack_ = true;
    }

    force_redraw_ = true;
  });

  layer1_dl_ = new ImDrawList(ImGui::GetDrawListSharedData());
  layer2_dl_ = new ImDrawList(ImGui::GetDrawListSharedData());
  layer3_dl_ = new ImDrawList(ImGui::GetDrawListSharedData());
}

void timeline_shutdown() {
  delete layer1_dl_;
  delete layer2_dl_;
  delete layer3_dl_;
  if (timeline_fb_)
    g_renderer->destroy_texture(timeline_fb_);
}

void timeline_redraw_window() {
  force_redraw_ = true;
}

void render_timeline() {
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  if (!controls::begin_window("Timeline 2", &g_timeline2_window_open)) {
    ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
    controls::end_window();
    return;
  }

  redraw_ = force_redraw_;
  if (force_redraw_)
    force_redraw_ = false;

  ImGuiIO& io = ImGui::GetIO();

  font_ = ImGui::GetFont();
  font_size_ = ImGui::GetFontSize();
  mouse_pos_ = ImGui::GetMousePos();
  mouse_wheel_ = io.MouseWheel;
  mouse_wheel_h_ = io.MouseWheelH;
  text_color_ = ImGui::GetColorU32(ImGuiCol_Text);
  text_transparent_color_ = Color(ImGui::GetColorU32(ImGuiCol_Text)).change_alpha(0.7f).to_uint32();
  holding_shift_ = ImGui::IsKeyDown(ImGuiKey_ModShift);
  holding_ctrl_ = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
  holding_alt_ = ImGui::IsKeyDown(ImGuiKey_ModAlt);
  timeline_focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

  ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding

  if (should_update_track_stack_) {
    timeline_update_track_stack();
    should_update_track_stack_ = false;
  }

  timeline_render_toolbar();

  ImGui::BeginGroup();
  timeline_render_navbar();

  timeline_layout_size_ = ImGui::GetContentRegionAvail();
  if (ImGui::BeginChild("timeline_content", ImVec2(), 0, ImGuiWindowFlags_NoBackground)) {
    ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
    vscroll_ = ImGui::GetScrollY();

    if (scroll_delta_y_ != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
      ImGui::SetScrollY(vscroll_ - scroll_delta_y_);
      scroll_delta_y_ = 0.0f;
      redraw_ = true;
    }

    if ((last_vscroll_ - vscroll_) != 0.0f)
      redraw_ = true;

    last_vscroll_ = vscroll_;
    timeline_render_splitter();
    timeline_render_track_panel();
    timeline_render_track_lanes();
    timeline_render_context_menu();
  }
  ImGui::EndChild();
  ImGui::EndGroup();

  controls::end_window();
}

void timeline_render_toolbar() {
  static const ImVec4 move_btn_color = ImColor(50, 190, 255);
  static const ImVec4 draw_btn_color = ImColor(181, 230, 29);
  static const ImVec4 slice_btn_color = ImColor(255, 150, 255);
  static const ImVec4 shift_btn_color = ImColor(147, 165, 255);
  static const ImVec4 stretch_btn_color = ImColor(50, 190, 255);

  bool move_tool = current_tool_ == TimelineTool::Move;
  bool select_tool = current_tool_ == TimelineTool::Select;
  bool draw_tool = current_tool_ == TimelineTool::Draw;
  bool slice_tool = current_tool_ == TimelineTool::Slice;
  bool shift_tool = current_tool_ == TimelineTool::Shift;

  constexpr ImVec2 icon_size = ImVec2(26.0f, 26.0f);
  const ImVec4 icon_color = Color(50, 190, 255).to_vec4();
  const ImVec4 selected_tool_color =
      ImGui::ColorConvertU32ToFloat4(Color(ImGui::GetStyleColorVec4(ImGuiCol_Button)).brighten(0.20f).to_uint32());

  constexpr uint32_t toolbar_child_flags =
      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX;
  constexpr uint32_t toolbar_window_flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  if (ImGui::BeginChild("##tl_toobar", ImVec2(), toolbar_child_flags, toolbar_window_flags)) {
    ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding

    font_push(FontType::Icon, 22.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Button(ICON_MS_MENU, icon_size)) {
      // TODO
    }

    ImGui::Separator();

    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);

    if (controls::outline_toggle_button(ICON_MS_TEXT_SELECT_END "##tl_select", move_tool, icon_color, icon_size)) {
      current_tool_ = TimelineTool::Move;
    }
    controls::item_tooltip("Select");

    if (controls::outline_toggle_button(ICON_MS_INK_SELECTION "##tl_select2", select_tool, icon_color, icon_size)) {
      current_tool_ = TimelineTool::Select;
    }
    controls::item_tooltip("Block select");

    if (controls::outline_toggle_button(ICON_MS_INK_HIGHLIGHTER_MOVE "##tl_draw", draw_tool, icon_color, icon_size)) {
      current_tool_ = TimelineTool::Draw;
    }
    controls::item_tooltip("Draw clip");

    if (controls::outline_toggle_button(ICON_MS_SURGICAL "##tl_slice", slice_tool, icon_color, icon_size)) {
      current_tool_ = TimelineTool::Slice;
    }
    controls::item_tooltip("Slice");

    if (controls::outline_toggle_button(ICON_MS_ARROWS_OUTWARD "##tl_shift", shift_tool, icon_color, icon_size)) {
      current_tool_ = TimelineTool::Shift;
    }
    controls::item_tooltip("Shift");

    ImGui::PopStyleVar();

    if (controls::outline_toggle_button(ICON_MS_EDIT_AUDIO "##tl_stretch", stretch_mode_, draw_btn_color, icon_size)) {
      stretch_mode_ = !stretch_mode_;
    }
    controls::item_tooltip("Strech mode");

    ImGui::Separator();

    if (ImGui::Button(ICON_MS_VARIABLE_ADD, icon_size)) {
      timeline_add_track();
    }
    ImGui::PopStyleVar();  // ImGuiStyleVar_FramePadding

    font_pop();
  } else {
    ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
  }

  ImGui::EndChild();
  ImGui::SameLine(0.0f, 2.0f);

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  im_draw_vline(
      draw_list,
      cursor_pos.x - 1.5f,
      cursor_pos.y,
      cursor_pos.y + ImGui::GetContentRegionAvail().y,
      ImGui::GetColorU32(ImGuiCol_Separator),
      2.0f);
}

void timeline_render_navbar() {
  static float separator_size = 2.0f;
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec2 region_size = ImGui::GetContentRegionAvail();
  float pos_x = ImGui::GetCursorPosX();
  float font_size = ImGui::GetFontSize();
  float timeline_w = timeline_display_size_.x;
  float navbar_w = region_size.x - track_panel_width_;
  float navbar_h = (font_size + style.FramePadding.y * 2.0f) * 2.0f;

  /*ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  if (ImGui::BeginChild("track_add", ImVec2(track_panel_width_, navbar_h - 1.0f), 0, track_control_window_flags_)) {
    ImVec2 region_avail = ImGui::GetContentRegionAvail();
    if (ImGui::Button("+ Track", ImVec2(region_avail.x, region_avail.y)))
      timeline_add_track();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  ImGui::SameLine(0.0f, 0.0f);*/

  IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(0.0f, 2.0f) })) {
    ImGui::SetCursorPosX(math::max(track_panel_width_, track_panel_min_width_) + separator_size + pos_x);
    ImGui::BeginGroup();

    if (controls::timeline_scrollbar("tl_hscroll", navbar_w, 0.1, song_duration_, &view_state_)) {
      redraw_ = true;
    }

    double time_pos = Engine2::playhead;
    double playback_start_pos = Engine2::get_playhead_start();
    controls::TimelineRulerResult tr_result = controls::timeline_ruler(
        "tl_ruler", grid_mode_, triplet_, timeline_w, song_duration_, playback_start_pos, &time_pos, &view_state_);
    if (tr_result != controls::TimelineRulerResult::None) {
      switch (tr_result) {
        case controls::TimelineRulerResult::Zoom: redraw_ = true; break;
        case controls::TimelineRulerResult::TimePositionChanged: Engine2::set_playhead_position(time_pos); break;
        default: break;
      }
    }

    ImGui::EndGroup();
  }

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  im_draw_hline(
      draw_list,
      cursor_pos.y - 1.5f,
      cursor_pos.x,
      cursor_pos.x + ImGui::GetContentRegionAvail().x,
      ImGui::GetColorU32(ImGuiCol_Separator),
      2.0f);
}

void timeline_render_splitter() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 content_min = ImGui::GetWindowContentRegionMin();
  ImVec2 content_max = ImGui::GetWindowContentRegionMax();
  ImVec2 splitter_pos = ImVec2(cursor_pos.x + track_panel_width_, cursor_pos.y + vscroll_);
  float content_height = content_max.y - content_min.y;

  ImGui::SetCursorScreenPos(ImVec2(cursor_pos.x + track_panel_width_, cursor_pos.y + vscroll_));
  ImGui::InvisibleButton("tl_splitter", ImVec2(4.0f, content_height));
  bool is_splitter_hovered = ImGui::IsItemHovered();
  bool is_splitter_active = ImGui::IsItemActive();
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Separator);

  // Change the color
  if (is_splitter_active) {
    color = ImGui::GetColorU32(ImGuiCol_SeparatorActive);
  } else if (is_splitter_hovered) {
    color = ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
  }

  if (is_splitter_hovered || is_splitter_active) {
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      track_panel_width_ = 150.0f;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }

  // Adjust splitter size
  if (is_splitter_active) {
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    track_panel_width_ += drag_delta.x;
    redraw_ = true;
  } else {
    track_panel_width_ = math::max(track_panel_width_, track_panel_min_width_);
  }

  im_draw_vline(dl, cursor_pos.x + track_panel_width_ + 0.5f, cursor_pos.y, splitter_pos.y + content_height, color, 2.0f);

  track_panel_pos_ = cursor_pos;
  track_lanes_pos_ = ImVec2(cursor_pos.x + track_panel_width_ + 2.0f, cursor_pos.y);
}

void timeline_render_track_panel() {
  static constexpr float vu_meter_width = 11.0f;
  static constexpr float track_color_width = 8.0f;
  static constexpr ImVec2 padding(6.0f, 2.0f);
  static constexpr ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);

  ImGui::SetCursorScreenPos(track_panel_pos_);

  const bool is_recording = Engine2::is_recording();
  uint32_t num_tracks = Engine2::tracks.size();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 top_left_cursor_pos = ImGui::GetCursorScreenPos();
  const auto& style = ImGui::GetStyle();
  bool open_track_context_menu = false;
  bool move_track = false;
  uint32_t move_track_src = 0;
  uint32_t move_track_dst = 0;
  std::optional<int32_t> resize_track_id;

  for (uint32_t i = 0; i < num_tracks; i++) {
    Track* track = Engine2::tracks[i];
    float height = track->get_height();
    bool shown = track->shown;
    ImVec2 color_size(track_color_width, height);
    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImVec2 end_pos = start_pos + ImVec2(track_panel_width_, height);
    ImVec2 track_color_min_bb = ImGui::GetCursorScreenPos();
    ImVec2 track_color_max_bb = track_color_min_bb + color_size;
    ImVec2 tmp_item_spacing = style.ItemSpacing;

    ImGui::PushID(i);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));

    // Reserve area for track color
    ImGui::Dummy(color_size);
    ImGui::SameLine(0.0f);

    // Draw track color
    if (ImGui::IsRectVisible(track_color_min_bb, track_color_max_bb)) {
      dl->AddRectFilled(track_color_min_bb, track_color_max_bb, track->color.to_uint32());
    }

    float track_controls_width = track_panel_width_ - track_color_width - vu_meter_width;
    ImVec2 track_controls_size(track_controls_width, height);
    ImVec2 track_controls_min_bb = ImGui::GetCursorScreenPos() + padding;
    ImVec2 track_controls_max_bb = track_controls_min_bb + track_controls_size;

    if (ImGui::BeginChild("##track_controls", track_controls_size, 0, track_control_window_flags_)) {
      float volume = track->ui_parameter_state.volume_db;
      bool mute = track->ui_parameter_state.mute;

      ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding

      IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(tmp_item_spacing) })) {
        const char* begin_name_str = track->name.c_str();
        const char* end_name_str = begin_name_str + track->name.size();

        IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(tmp_item_spacing.x, 2.0f) })) {
          if (controls::collapse_button2("##track_collapse", &track->shown)) {
            redraw_ = true;
          }
          ImGui::SameLine(0.0f, 4.0f);

          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
          if (!track->name.empty()) {
            ImGui::TextUnformatted(begin_name_str, end_name_str);
          } else {
            ImGui::BeginDisabled();
            ImGui::TextUnformatted("(unnamed)");
            ImGui::EndDisabled();
          }
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload("WB_MOVE_TRACK", &i, sizeof(uint32_t), ImGuiCond_Once);
          ImGui::Text("Move track: %s", begin_name_str);
          ImGui::EndDragDropSource();
        }

        ImVec2 free_region = ImGui::GetContentRegionAvail();
        float item_height = controls::get_item_height();

        if (free_region.y < item_height * 1.5f) [[likely]] {
          if (free_region.y < (item_height - style.ItemSpacing.y)) {
            // Very compact
            if (free_region.y >= item_height * 0.5f) {
              if (controls::small_toggle_button("M", &mute, muted_color))
                track->set_mute(!mute);
              ImGui::SameLine(0.0f, 2.0f);
              if (ImGui::SmallButton("S"))
                Engine2::solo_track(i);

              ImGui::SameLine(0.0f, 2.0f);
              ImGui::BeginDisabled(is_recording);
              if (controls::small_toggle_button("R", &track->input_attr.armed, muted_color))
                Engine2::set_track_recording_state(i, !track->input_attr.armed);
              if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("track_input_context_menu");
              ImGui::EndDisabled();
            }
          } else [[likely]] {
            // Compact
            if (controls::toggle_button("M", &mute, muted_color))
              track->set_mute(!mute);

            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button("S"))
              Engine2::solo_track(i);

            ImGui::SameLine(0.0f, 2.0f);
            ImGui::BeginDisabled(is_recording);
            if (controls::toggle_button("R", &track->input_attr.armed, muted_color))
              Engine2::set_track_recording_state(i, !track->input_attr.armed);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
              ImGui::OpenPopup("track_input_context_menu");
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 2.0f);
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (controls::param_drag_db("##track_vol", &volume))
              track->set_volume(volume);
          }
        } else {
          // Large
          if (controls::param_drag_db("Vol.", &volume))
            track->set_volume(volume);

          if (free_region.y >= item_height * 2.5f) {
            float pan = track->ui_parameter_state.pan;
            if (controls::param_drag_panning("Pan", &pan)) {
              track->set_pan(pan);
            }
          }

          if (free_region.y >= item_height * 3.5f) {
            IMGUI_STYLE_BLOCK(({ ImStyleWindowPadding(8.0f, 3.0f) })) {
              const char* input_name = "None";
              switch (track->input.type) {
                case TrackInputType::ExternalStereo: {
                  uint32_t index_mul = track->input.index * 2;
                  ImFormatStringToTempBuffer(&input_name, nullptr, "%d+%d", index_mul + 1, index_mul + 2);
                  break;
                }
                case TrackInputType::ExternalMono: {
                  ImFormatStringToTempBuffer(&input_name, nullptr, "%d", track->input.index + 1);
                  break;
                }
                default: break;
              }

              ImGui::BeginDisabled(is_recording);
              if (ImGui::BeginCombo("Input", input_name)) {
                track_input_context_menu(track, i);
                ImGui::EndCombo();
              }
              ImGui::EndDisabled();
            }
          }

          if (controls::small_toggle_button("M", &mute, muted_color))
            track->set_mute(!mute);
          ImGui::SameLine(0.0f, 2.0f);
          if (ImGui::SmallButton("S"))
            Engine2::solo_track(i);
          ImGui::SameLine(0.0f, 2.0f);

          ImGui::BeginDisabled(is_recording);
          if (controls::small_toggle_button("R", &track->input_attr.armed, muted_color))
            Engine2::set_track_recording_state(i, !track->input_attr.armed);
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("track_input_context_menu");
          ImGui::EndDisabled();

          font_push(FontType::Icon, 13.0f);
          ImGui::SameLine(0.0f, 2.0f);
          if (ImGui::SmallButton(ICON_MS_POWER))
            ImGui::OpenPopup("track_plugin_context_menu");
          font_pop();
        }

        if (ImGui::BeginPopup("track_input_context_menu")) {
          track_input_context_menu(track, i);
          ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("track_plugin_context_menu")) {
          track_plugin_context_menu(track);
          ImGui::EndPopup();
        }

        if (ImGui::IsWindowHovered() && !(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          context_menu_track_ = track;
          context_menu_track_id_ = i;
          tmp_color_ = track->color;
          tmp_name_ = track->name;
          open_track_context_menu = true;
        }
      }
    } else {
      ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
    }

    ImGui::EndChild();

    if (ImGui::BeginDragDropTarget()) {
      static constexpr auto drag_drop_flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

      // Custom highlighter
      if (auto payload = ImGui::AcceptDragDropPayload("WB_MOVE_TRACK", ImGuiDragDropFlags_AcceptPeekOnly)) {
        dl->AddLine(start_pos, ImVec2(end_pos.x, start_pos.y), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
      } else if (ImGui::GetDragDropPayload()) {
        dl->AddRect(start_pos, end_pos, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 0, 2.0f);
      }

      if (auto payload = ImGui::AcceptDragDropPayload("WB_PLUGINDROP", drag_drop_flags)) {
        PluginItem* item;
        std::memcpy(&item, payload->Data, payload->DataSize);
        // add_plugin(track, item->uid);
      } else if (auto payload = ImGui::AcceptDragDropPayload("WB_MOVE_TRACK", drag_drop_flags)) {
        assert(payload->DataSize == sizeof(uint32_t));
        uint32_t* source = (uint32_t*)payload->Data;
        if (i != *source) {
          move_track = true;
          move_track_src = *source;
          move_track_dst = i;
        }
      }

      ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    controls::level_meter(
        "##timeline_vu_meter", ImVec2(10.0f, height), 2, track->level_meter, track->level_meter_color, false);

    if (controls::hsplitter(i, &height, 60.0f, 20.f, 600.f, track_panel_width_)) {
      if (track->shown)
        track->height = height;
      resize_track_id = i;
      redraw_ = true;
    }

    ImGui::PopStyleVar();  // ImGuiStyleVar_ItemSpacing
    ImGui::PopID();
  }

  if (resize_track_id) {
    timeline_update_track_stack(resize_track_id.value());
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  if (ImGui::BeginChild("##tl_add_track_child", ImVec2(track_panel_width_, 60.0f), 0, track_control_window_flags_)) {
    ImVec2 size = ImGui::GetContentRegionAvail();
    static const ImVec4 add_track_btn_col = Color(147, 165, 255).to_vec4();
    font_push(FontType::Icon, 22.0f);
    controls::outline_toggle_button(ICON_MS_ADD "##tl_add_track", false, add_track_btn_col, ImVec2(size.x, 0.0f));
    font_pop();
  }
  ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
  ImGui::EndChild();

  if (move_track) {
    CmdMoveTrack* cmd = new CmdMoveTrack();
    cmd->src_slot = move_track_src;
    cmd->dst_slot = move_track_dst;
    CommandManager2::execute_command("Move track", cmd);
    redraw_ = true;
  }

  if (open_track_context_menu) {
    ImGui::OpenPopup("track_context_menu");
  }

  bool open_rename_track_dialog = false;
  bool open_change_color_dialog = false;

  if (ImGui::BeginPopup("track_context_menu")) {
    if (auto ret = track_context_menu(context_menu_track_, context_menu_track_id_, &tmp_name_, &tmp_color_);
        ret != TrackContextMenuResult::None) {
      switch (ret) {
        case TrackContextMenuResult::Rename: open_rename_track_dialog = true; break;
        case TrackContextMenuResult::ChangeColor: open_change_color_dialog = true; break;
        case TrackContextMenuResult::Done: context_menu_track_ = nullptr; break;
      }
      redraw_ = true;
    }
    ImGui::EndPopup();
  }

  if (context_menu_track_) {
    if (open_rename_track_dialog) {
      ImGui::OpenPopup("tl_rename_trk");
    }

    if (open_change_color_dialog) {
      ImGui::OpenPopup("tl_pick_color_trk");
    }

    if (auto ret = rename_dialog("tl_rename_trk", tmp_name_, &context_menu_track_->name); ret != ConfirmDialog::None) {
      switch (ret) {
        case ConfirmDialog::Ok: {
          CmdRenameTrack* cmd = new CmdRenameTrack();
          cmd->track_id = context_menu_track_id_;
          cmd->new_name = context_menu_track_->name;
          cmd->old_name = tmp_name_;
          CommandManager2::execute_command("Rename track", cmd);
          redraw_ = true;
          break;
        }
        default: break;
      }
    }

    if (auto ret = color_picker_dialog("tl_pick_color_trk", tmp_color_, &context_menu_track_->color);
        ret != ConfirmDialog::None) {
      switch (ret) {
        case ConfirmDialog::Ok: {
          CmdChangeTrackColor* cmd = new CmdChangeTrackColor();
          cmd->track_id = context_menu_track_id_;
          cmd->new_color = context_menu_track_->color.to_uint32();
          cmd->old_color = tmp_color_.to_uint32();
          CommandManager2::execute_command("Change track color", cmd);
          redraw_ = true;
          break;
        }
        default: break;
      }
    }
  }

  track_lanes_height_ = ImGui::GetCursorPos().y;
  // Log::debug("{} {}", ImGui::GetCursorPos().y, track_lanes_height_);
}

void timeline_render_track_lanes() {
  ImGui::SetCursorScreenPos(track_lanes_pos_);

  ImVec2 available_size = ImGui::GetContentRegionAvail();
  const float view_width = available_size.x;

  view_pos_ = ImGui::GetCursorScreenPos();
  view_min_ = ImVec2(view_pos_.x, vscroll_ + view_pos_.y);
  view_max_ = ImVec2(view_pos_.x + available_size.x, vscroll_ + view_pos_.y + timeline_layout_size_.y);

  ImVec2 fb_scale = ImGui::GetWindowViewport()->FramebufferScale;
  ImVec2 view_size(available_size.x, math::max(track_lanes_height_, timeline_layout_size_.y));
  ImVec2 display_size = view_max_ - view_min_;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  ImGui::PushClipRect(view_min_, view_max_, true);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  ImGui::InvisibleButton("##timeline_view", view_size, timeline_mouse_btn_flags_);
  timeline_hovered_ = ImGui::IsItemHovered();
  ImGui::PopStyleVar();

  if (display_size.x != timeline_display_size_.x || display_size.y != timeline_display_size_.y ||
      current_fb_scale_.x != fb_scale.x || current_fb_scale_.y != fb_scale.y) {
    int width = (int)math::max(display_size.x * fb_scale.x, 16.0f);
    int height = (int)math::max(display_size.y * fb_scale.y, 16.0f);
    if (timeline_fb_)
      g_renderer->destroy_texture(timeline_fb_);
    timeline_fb_ = g_renderer->create_texture(
        GPUTextureUsage::Sampled | GPUTextureUsage::RenderTarget,
        GPUFormat::UnormB8G8R8A8,
        width,
        height,
        true,
        0,
        0,
        nullptr);
    timeline_display_size_ = display_size;
    current_fb_scale_ = fb_scale;
    redraw_ = true;
    Log::debug("Timeline framebuffer resized ({}x{})", (int)width, (int)height);
  }

  view_scale_ = timeline_get_view_scale();
  timeline_handle_mouse_event();
  timeline_handle_track_event();
  timeline_handle_key_event();

  view_scale_ = timeline_get_view_scale();
  const double scroll_pos_x = timeline_get_scroll_pos_x();
  const double inv_view_scale = 1.0 / view_scale_;
  scroll_offset_x_ = (double)view_pos_.x - scroll_pos_x;

  if (redraw_) {
    timeline_draw_track_lanes(display_size, view_size);
  }

  ImTextureID fb_tex_id = (ImTextureID)timeline_fb_;
  dl->AddImage(fb_tex_id, view_min_, view_min_ + display_size);

  if (Engine2::is_playing()) {
    const double playhead_offset = Engine2::playhead * inv_view_scale;
    const float playhead_pos = (float)math::round(scroll_offset_x_ + playhead_offset);
    im_draw_vline(dl, playhead_pos, view_min_.y, view_max_.y, playhead_color_);
  }

  ImGui::PopClipRect();

  timeline_render_floating_btns();
}

void timeline_render_floating_btns() {
  if (!tl_state_.in_action() && tl_state_.select.is_selected) {
    const double inv_view_scale = 1.0 / view_scale_;
    const float selection_min_y = track_stack_[tl_state_.select.first_track_id];
    const float selection_max_y = timeline_get_track_end_pos_y(tl_state_.select.last_track_id);
    const double min_pos_x = scroll_offset_x_ + tl_state_.select.start_pos * inv_view_scale;
    const double max_pos_x = scroll_offset_x_ + tl_state_.select.end_pos * inv_view_scale;
    const float min_pos_y = view_min_.y - vscroll_ + selection_min_y + 4.0f;
    const float max_pos_y = view_min_.y - vscroll_ + selection_max_y + 4.0f;

    if (max_pos_x >= view_min_.x && min_pos_x < view_max_.x && max_pos_y >= view_min_.y && min_pos_y < view_max_.y) {
      float x = math::clamp((float)math::round(min_pos_x), view_min_.x + 4.0f, view_max_.x - floating_button_size_.x - 4.0f);
      float y = math::min(max_pos_y, view_max_.y - 32.0f);
      ImVec4 border_color = Color(ImGui::GetColorU32(ImGuiCol_Border)).brighten(0.25f).to_vec4();
      ImVec2 pos((float)x, y);

      IMGUI_STYLE_BLOCK(({
        ImStyleWindowPadding(0.0f, 0.0f),
        ImStyleFramePadding(0.0f, 0.0f),
        ImStyleFrameRounding(0.0f),
      })) {
        ImGui::PushStyleColor(ImGuiCol_Border, border_color);

        if (controls::begin_floating_window("tl_float_btns", pos)) {
          static constexpr ImVec2 btn_size(28.0f, 28.0f);
          font_push(FontType::Icon, 24.0f);

          if (ImGui::Button(ICON_MS_MUSIC_NOTE_ADD, btn_size)) {
            timeline_add_midi_clips();
          }
          controls::item_tooltip("Create MIDI clips");
          ImGui::SameLine(0.0f, 0.0f);

          // ImGui::Button(ICON_MS_TIMELINE, btn_size);
          // controls::item_tooltip("Create automation clips");
          // ImGui::SameLine(0.0f, 0.0f);

          if (ImGui::Button(ICON_MS_REMOVE_SELECTION, btn_size)) {
            timeline_delete_region();
          }
          controls::item_tooltip("Delete region");
          ImGui::SameLine(0.0f, 0.0f);

          // ImGui::Button(ICON_MS_SURGICAL, btn_size);
          // controls::item_tooltip("Slice region");

          font_pop();
          floating_button_size_ = ImGui::GetWindowSize();
        }

        ImGui::PopStyleColor();
      }

      controls::end_floating_window();
    }
  }
}

void timeline_render_context_menu() {
  bool open_rename_popup = false;
  bool open_change_color_popup = false;

  if (ImGui::BeginPopup("clip_ctx_menu")) {
    if (context_menu_track_ && context_menu_clip_) {
      if (ImGui::MenuItem("Rename")) {
        tmp_name_ = context_menu_clip_->name;
        open_rename_popup = true;
      }

      if (ImGui::MenuItem("Change color")) {
        open_change_color_popup = true;
      }

      if (ImGui::MenuItem("Delete", "Del")) {
        CmdDeleteClip* cmd = new CmdDeleteClip();
        cmd->track_id = context_menu_track_id_;
        cmd->clip_id = context_menu_clip_->id;
        CommandManager2::execute_command("Delete clip", cmd);
        selected_clip_.reset();
      }

      if (ImGui::MenuItem("Duplicate", WB_HKEY_STR_CTRL "D")) {
      }
    }
    ImGui::EndPopup();
  }

  if (open_rename_popup)
    ImGui::OpenPopup("rename_clip");
  else if (open_change_color_popup)
    ImGui::OpenPopup("clip_change_color_popup");

  if (context_menu_clip_) {
    if (auto ret = rename_dialog("rename_clip", tmp_name_, &context_menu_clip_->name)) {
      switch (ret) {
        case ConfirmDialog::ValueChanged: force_redraw_ = true; break;
        case ConfirmDialog::Ok: {
          CmdRenameClip* cmd = new CmdRenameClip();
          cmd->track_id = context_menu_track_id_;
          cmd->clip_id = context_menu_clip_->id;
          cmd->new_name = context_menu_clip_->name;
          cmd->old_name = tmp_name_;
          CommandManager2::execute_command("Rename clip", cmd);
          context_menu_track_ = nullptr;
          context_menu_clip_ = nullptr;
          force_redraw_ = true;
          break;
        }
        case ConfirmDialog::Cancel:
          context_menu_track_ = nullptr;
          context_menu_clip_ = nullptr;
          force_redraw_ = true;
          break;
        case ConfirmDialog::None: break;
        default: break;
      }
    }
  }
}

void timeline_handle_mouse_event() {
  // const bool timeline_clicked = ImGui::IsItemClicked();
  const bool is_active = ImGui::IsItemActive();
  const bool is_activated = ImGui::IsItemActivated();
  left_mouse_clicked_ = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  left_mouse_down_ = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  middle_mouse_clicked_ = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  middle_mouse_down_ = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  right_mouse_clicked_ = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  right_mouse_down_ = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Right);
  can_select_ = holding_ctrl_ && !holding_shift_;

  // Ctrl+MouseWheel Zoom
  if (timeline_hovered_ && holding_ctrl_ && mouse_wheel_ != 0.0f) {
    view_state_.zoom(mouse_wheel_ * zoom_sensitivity_, mouse_pos_.x - view_pos_.x, song_duration_, view_scale_);
    view_scale_ = timeline_get_view_scale();
    redraw_ = true;
  }

  // Horizontal scroll with horizontal mouse wheel/gesture
  if (timeline_hovered_ && mouse_wheel_h_ != 0.0f) {
    float drag_sensitivity = 64.0f;
    timeline_hscroll(mouse_wheel_h_ * drag_sensitivity);
    redraw_ = true;
  }

  // Horizontal scroll with middle mouse button
  if (middle_mouse_clicked_) {
    scrolling_ = true;
  }

  if (scrolling_) {
    const ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
    timeline_hscroll(drag_delta.x);
    scroll_delta_y_ = drag_delta.y;
    redraw_ = true;
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
  }

  if (!middle_mouse_down_) {
    scroll_delta_y_ = 0.0f;
    scrolling_ = false;
  }

  // Handle file drag & drop
  BrowserFilePayload* drop_payload_data{};
  bool dragging_file = false;
  bool item_dropped = false;
  if (ImGui::BeginDragDropTarget()) {
    auto payload = ImGui::GetDragDropPayload();
    if (payload->IsDataType("WB_FILEDROP")) {
      item_dropped = ImGui::AcceptDragDropPayload("WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
      std::memcpy(&drop_payload_data, payload->Data, payload->DataSize);
      dragging_file = true;
    }
    ImGui::EndDragDropTarget();
  }

  bool requires_drag = any_of(
      tl_state_.type,
      TimelineState::Select,
      TimelineState::Move,
      TimelineState::Duplicate,
      TimelineState::Resize,
      TimelineState::Stretch,
      TimelineState::Nudge);

  // Handle automatic scrolling
  if (dragging_file || requires_drag) {
    static constexpr float drag_offset_x = 20.0f;
    static constexpr float drag_offset_y = 40.0f;
    float speed = 10.0f * GImGui->IO.DeltaTime;  // Make sure this is not frame-dependent
    float min_offset_x;
    float max_offset_x;
    float min_offset_y;
    float max_offset_y;

    if (!dragging_file) {
      min_offset_x = view_min_.x;
      max_offset_x = view_max_.x;
      min_offset_y = view_min_.y;
      max_offset_y = view_max_.y;
    } else {
      min_offset_x = view_min_.x + drag_offset_x;
      max_offset_x = view_max_.x - drag_offset_x;
      min_offset_y = view_min_.y + drag_offset_y;
      max_offset_y = view_max_.y - drag_offset_y;
    }

    if (mouse_pos_.x < min_offset_x) {
      float distance = min_offset_x - mouse_pos_.x;
      timeline_hscroll(distance * speed);
    }
    if (mouse_pos_.x > max_offset_x) {
      float distance = max_offset_x - mouse_pos_.x;
      timeline_hscroll(distance * speed);
    }
    if (mouse_pos_.y < min_offset_y) {
      float distance = min_offset_y - mouse_pos_.y;
      scroll_delta_y_ = distance * speed;
    }
    if (mouse_pos_.y > max_offset_y) {
      float distance = max_offset_y - mouse_pos_.y;
      scroll_delta_y_ = distance * speed;
    }
  }

  if (dragging_file) {
    tl_state_.type = TimelineState::DragDropFiles;
    tl_state_.drag_drop_files = {
      .payload_data = drop_payload_data,
      .first_track_id = -1,
      .item_dropped = item_dropped,
    };
  }

  if (timeline_hovered_ && can_select_) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
  }

  if (tl_state_.type == TimelineState::DragDropFiles && !dragging_file) {
    hovered_track_id_.reset();
    tl_state_.end_action();
  }
}

void timeline_handle_key_event() {
  if (!timeline_focused_) {
    return;
  }

  if (hkey_pressed(Hotkey::Delete)) {
    // try delete region first
    if (!timeline_delete_region()) {
      if (selected_clip_) {
        CmdDeleteClip* cmd = new CmdDeleteClip();
        cmd->track_id = selected_clip_->first;
        cmd->clip_id = selected_clip_->second;
        selected_clip_.reset();
        CommandManager2::execute_command("Delete clip", cmd);
      }
    }
  }

  if (hkey_pressed(Hotkey::TimelineAddMidiClips)) {
    timeline_add_midi_clips();
  }
}

void timeline_handle_track_event() {
  float track_pos_y = -vscroll_;
  float first_visible_pos_y = INFINITY;
  float relative_mouse_pos_y = mouse_pos_.y - view_min_.y;
  const double inv_view_scale = 1.0 / view_scale_;
  const double hovered_position = timeline_get_hovered_position();
  const float clip_label_height = font_size_ + 5.0f;
  const bool require_drag =
      !any_of(tl_state_.type, TimelineState::Select, TimelineState::DragDropFiles, TimelineState::Move);

  int32_t track_idx = 0;
  int32_t track_count = (int32_t)Engine2::tracks.size();
  bool is_selecting = tl_state_.type == TimelineState::Select;
  bool is_selected = tl_state_.select.is_selected;
  const int32_t first_selected_track = tl_state_.select.first_track_id;
  const int32_t last_selected_track = tl_state_.select.last_track_id;
  const double selection_start_pos = tl_state_.select.start_pos;
  const double selection_end_pos = tl_state_.select.end_pos;
  std::optional<Pair<int32_t, uint32_t>> selected_clip;

  for (; track_idx < track_count; track_idx++) {
    Track* track = Engine2::tracks[track_idx];
    const float height = track->get_height();
    const float track_view_min_y = -height - track_separator_height_;
    const float next_pos_y = track_pos_y + height + track_separator_height_;

    if (track_pos_y > timeline_display_size_.y) {
      break;
    }

    if (track_pos_y < track_view_min_y) {
      track_pos_y = next_pos_y;
      continue;
    }

    ImVec2 track_min(view_min_.x, view_min_.y + track_pos_y);
    ImVec2 track_max(view_max_.x, view_min_.y + next_pos_y);
    bool rect_hovered = ImGui::IsMouseHoveringRect(track_min, track_max, require_drag);
    bool hovered = rect_hovered && timeline_hovered_;

    if (first_visible_pos_y == INFINITY) {
      first_visible_pos_y = track_pos_y;
      first_visible_track_ = track_idx;
    }

    if (hovered) {
      if (is_selected && left_mouse_clicked_) {
        if (!math::in_range_inclusive(track_idx, first_selected_track, last_selected_track) ||
            !math::in_range(hovered_position, selection_start_pos, selection_end_pos)) {
          tl_state_.clear_selection();
          is_selected = false;
          redraw_ = true;
        }
      }

      hovered_track_id_ = track_idx;

      float content_start_pos_y = track_min.y + clip_label_height;
      bool mouse_inside_label_area = math::in_range(mouse_pos_.y, track_min.y, content_start_pos_y);
      bool mouse_inside_content_area = math::in_range(mouse_pos_.y, content_start_pos_y, track_max.y);
      uint32_t num_clips = (uint32_t)track->clips.size();
      bool can_select = true;

      for (uint32_t i = 0; i < num_clips; i++) {
        Clip* clip = track->clips[i];
        const double start_pos = clip->min_time * inv_view_scale;
        const double end_pos = clip->max_time * inv_view_scale;
        const float x0_min = (float)(scroll_offset_x_ + math::round(start_pos));
        const float x1_max = (float)(scroll_offset_x_ + math::round(end_pos));

        if (x0_min >= view_max_.x)
          break;
        if (x1_max < view_min_.x)
          continue;

        constexpr float handle_size = 6.0f;
        const float x0_max = x0_min + handle_size;
        const float x1_min = x1_max - handle_size;

        if (math::in_range(mouse_pos_.x, x0_min, x1_max)) {
          if (left_mouse_clicked_ || right_mouse_clicked_) {
            selected_clip.emplace(track_idx, clip->id);
          }

          if (mouse_inside_label_area) {
            if (math::in_range(mouse_pos_.x, x0_min, x0_max)) {
              if (left_mouse_clicked_) {
                if (ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                  tl_state_.type = TimelineState::Stretch;
                } else if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                  tl_state_.type = TimelineState::Nudge;
                } else {
                  tl_state_.type = TimelineState::Resize;
                }

                tl_state_.initial_pos = hovered_position;
                tl_state_.tool.initial_track_id = track_idx;
                tl_state_.tool.clip_id = i;
                tl_state_.tool.left_side = true;

                if (is_selected) {
                  timeline_prepare_resize(clip->min_time, true);
                }
              }
              ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
              can_select = false;
            } else if (math::in_range(mouse_pos_.x, x1_min, x1_max)) {
              if (left_mouse_clicked_) {
                if (ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                  tl_state_.type = TimelineState::Stretch;
                } else if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                  tl_state_.type = TimelineState::Nudge;
                } else {
                  tl_state_.type = TimelineState::Resize;
                }

                tl_state_.initial_pos = hovered_position;
                tl_state_.tool.initial_track_id = track_idx;
                tl_state_.tool.clip_id = i;
                tl_state_.tool.left_side = false;

                if (is_selected) {
                  timeline_prepare_resize(clip->max_time, false);
                }
              }
              ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
              can_select = false;
            } else {
              if (left_mouse_clicked_) {
                tl_state_.type = ImGui::IsKeyDown(ImGuiMod_Shift) ? TimelineState::Duplicate : TimelineState::Move;
                tl_state_.initial_pos = hovered_position;
                tl_state_.tool = {
                  .initial_track_id = track_idx,
                  .clip_id = i,
                  .min_relative_ofs = is_selected ? -selection_start_pos : -clip->min_time,
                };
              }
              ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
              can_select = false;
            }
          } else {
            if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
              if (left_mouse_clicked_) {
                tl_state_.type = TimelineState::Shift;
                tl_state_.initial_pos = hovered_position;
                tl_state_.tool = {
                  .initial_track_id = track_idx,
                  .clip_id = i,
                };
              }
              ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
              can_select = false;
            } else if (ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
              can_select = false;
            }
          }
        }
      }

      if (can_select) {
        if (left_mouse_clicked_) {
          tl_state_.type = TimelineState::Select;
          tl_state_.select = {
            .start_pos = hovered_position,
            .first_track_id = track_idx,
            .last_track_id = track_idx,
          };
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
      }
    }

    /*if (timeline_state_.type == TimelineState::Select) {
      if (left_mouse_down_ && hovered) {
        timeline_state_.select.end_pos = hovered_position;
        timeline_state_.select.last_track_id = track_idx;
      }
    }*/

    if (tl_state_.type == TimelineState::DragDropFiles) {
      if (rect_hovered) {
        tl_state_.drag_drop_files.first_track_id = track_idx;
        tl_state_.drag_drop_files.position = hovered_position;
      }
    }

    track_pos_y = next_pos_y;
  }

  first_visible_track_pos_y_ = first_visible_pos_y;
  last_visible_track_ = track_idx;

  if (left_mouse_clicked_ || right_mouse_clicked_) {
    if (selected_clip) {
      clip_editor_set_clip(selected_clip->first, selected_clip->second);
      selected_clip_ = std::move(selected_clip);
    } else {
      clip_editor_unset_clip();
      selected_clip_.reset();
    }
    redraw_ = true;
  }

  if (right_mouse_clicked_ && selected_clip) {
    context_menu_track_ = Engine2::tracks[selected_clip->first];
    context_menu_clip_ = context_menu_track_->clips[selected_clip->second];
    ImGui::OpenPopup("clip_ctx_menu");
  }

  switch (tl_state_.type) {
    case TimelineState::DragDropFiles:
      if (tl_state_.drag_drop_files.item_dropped) {
        hovered_track_id_.reset();
        timeline_add_clip_from_file();
      }
      redraw_ = true;
      break;
    case TimelineState::Select:
      ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

      if (left_mouse_down_ && hovered_track_id_) {
        tl_state_.select.last_track_id = hovered_track_id_.value();
      }

      if (left_mouse_down_) {
        tl_state_.select.end_pos = hovered_position;
        redraw_ = true;
      }

      if (!left_mouse_down_) {
        auto& [is_selected, start_pos, end_pos, first_track_id, last_track_id] = tl_state_.select;
        if (start_pos != end_pos) {
          is_selected = true;
          if (start_pos > end_pos) {
            std::swap(start_pos, end_pos);
          }
          if (first_track_id > last_track_id) {
            std::swap(first_track_id, last_track_id);
          }
          timeline_query_selected_range();
        }
        redraw_ = true;
        hovered_track_id_.reset();
        tl_state_.end_action();
      }

      break;
    case TimelineState::Move:
    case TimelineState::Duplicate:
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

      if (!left_mouse_down_) {
        if (tl_state_.select.is_selected) {
          int32_t hovered_track = hovered_track_id_.value();
          int32_t track_size = (int32_t)Engine2::tracks.size();
          int32_t src_track = tl_state_.tool.initial_track_id;
          int32_t min_track_move = src_track - first_selected_track;
          int32_t max_track_move = track_size - (last_selected_track - src_track) - 1;
          int32_t track_relative_ofs = math::clamp(hovered_track, min_track_move, max_track_move) - src_track;
          double relative_pos = math::max(hovered_position - tl_state_.initial_pos, tl_state_.tool.min_relative_ofs);
          CmdMoveClips* cmd = new CmdMoveClips();

          cmd->clip_spans = tl_state_.selected_track_clips;
          cmd->src_track_id = tl_state_.select.first_track_id;
          cmd->dst_track_relative_ofs = track_relative_ofs;
          cmd->start_pos = tl_state_.select.start_pos;
          cmd->end_pos = tl_state_.select.end_pos;
          cmd->relative_move_ofs = relative_pos;
          cmd->duplicate = tl_state_.type == TimelineState::Duplicate;
          CommandManager2::execute_command("Move clips", cmd);

          tl_state_.select.first_track_id += track_relative_ofs;
          tl_state_.select.last_track_id += track_relative_ofs;
          tl_state_.select.start_pos += relative_pos;
          tl_state_.select.end_pos += relative_pos;
          timeline_query_selected_range();
        } else {
          int32_t src_track = tl_state_.tool.initial_track_id;
          Track* track = Engine2::tracks[src_track];
          Clip* clip = track->clips[tl_state_.tool.clip_id];
          double relative_pos = math::max(hovered_position - tl_state_.initial_pos, -clip->min_time);

          Vector<ClipSpan> clip_span;
          new (clip_span.emplace_back_raw()) ClipSpan{
            .contains_clip = true,
            .first = tl_state_.tool.clip_id,
            .last = tl_state_.tool.clip_id,
            .first_offset = 0.0,
            .last_offset = 0.0,
          };

          CmdMoveClips* cmd = new CmdMoveClips();
          cmd->clip_spans = std::move(clip_span);
          cmd->src_track_id = src_track;
          cmd->dst_track_relative_ofs = hovered_track_id_.value() - src_track;
          cmd->start_pos = clip->min_time;
          cmd->end_pos = clip->max_time;
          cmd->relative_move_ofs = relative_pos;
          cmd->duplicate = tl_state_.type == TimelineState::Duplicate;
          CommandManager2::execute_command("Move clips", cmd);
        }

        hovered_track_id_.reset();
        tl_state_.end_action();
      }

      redraw_ = true;
      break;
    case TimelineState::Shift:
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      if (!left_mouse_down_) {
        hovered_track_id_.reset();
        tl_state_.end_action();
      }

      redraw_ = true;
      break;
    case TimelineState::Resize:
    case TimelineState::Stretch:
    case TimelineState::Nudge:
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      redraw_ = true;

      if (!left_mouse_down_) {
        double min_length = 1.0 / get_grid_division(inv_view_scale, grid_mode_, triplet_);
        double relative_ofs = hovered_position - tl_state_.initial_pos;

        if (relative_ofs == 0.0) {
          hovered_track_id_.reset();
          tl_state_.end_action();
          break;
        }

        if (tl_state_.select.is_selected) {
          CmdResizeClips* cmd = new CmdResizeClips();
          cmd->first_track = tl_state_.select.first_track_id;
          cmd->clips = std::move(tl_state_.resize_clips);
          cmd->relative_ofs = hovered_position - tl_state_.initial_pos;
          cmd->min_clip_length = min_length;
          cmd->min_relative_ofs = tl_state_.tool.min_relative_ofs;
          cmd->mode = tl_state_.get_resize_mode();
          cmd->left_side = tl_state_.tool.left_side;

          switch (cmd->mode) {
            case ClipResizeMode::Resize: CommandManager2::execute_command("Resize clips", cmd); break;
            case ClipResizeMode::Stretch: CommandManager2::execute_command("Stretch clips", cmd); break;
            case ClipResizeMode::Nudge: CommandManager2::execute_command("Nudge clips", cmd); break;
            default: WB_UNREACHABLE();
          }
        } else {
          bool left_side = tl_state_.tool.left_side;
          Track* track = Engine2::tracks[tl_state_.tool.initial_track_id];
          Clip* clip = track->clips[tl_state_.tool.clip_id];
          CmdResizeClips* cmd = new CmdResizeClips();
          cmd->first_track = tl_state_.tool.initial_track_id;
          cmd->relative_ofs = hovered_position - tl_state_.initial_pos;
          cmd->min_clip_length = min_length;
          cmd->min_relative_ofs = left_side ? clip->max_time : clip->min_time;
          cmd->mode = tl_state_.get_resize_mode();
          cmd->left_side = tl_state_.tool.left_side;
          cmd->clips.emplace_back(true, clip->id);

          switch (cmd->mode) {
            case ClipResizeMode::Resize: CommandManager2::execute_command("Resize clips", cmd); break;
            case ClipResizeMode::Stretch: CommandManager2::execute_command("Stretch clips", cmd); break;
            case ClipResizeMode::Nudge: CommandManager2::execute_command("Nudge clips", cmd); break;
            default: WB_UNREACHABLE();
          }
        }

        hovered_track_id_.reset();
        tl_state_.end_action();
      }
      break;
    default:
      hovered_track_id_.reset();
      tl_state_.end_action();
      break;
  }

  if (tl_state_.select.is_selected && left_mouse_clicked_ && !tl_state_.in_action()) {
    tl_state_.clear_selection();
    redraw_ = true;
  }
}

void timeline_prepare_resize(double resize_pos, bool left) {
  int32_t first_selected_track = tl_state_.select.first_track_id;
  int32_t last_selected_track = tl_state_.select.last_track_id;

  tl_state_.resize_clips.resize(0);

  for (int32_t i = first_selected_track; i <= last_selected_track; i++) {
    TrackClipResizeInfo clip_resize{};
    Track* track = Engine2::tracks[i];
    int32_t clip_span_idx = i - first_selected_track;
    const ClipSpan& clip_span = tl_state_.selected_track_clips[clip_span_idx];

    if (clip_span.contains_clip) {
      for (uint32_t j = clip_span.first; j <= clip_span.last; j++) {
        Clip* clip = track->clips[j];

        if (left) {
          if (clip->min_time == resize_pos) {
            clip_resize = {
              .should_resize = true,
              .clip_id = j,
            };
          }
        } else {
          if (clip->max_time == resize_pos) {
            clip_resize = {
              .should_resize = true,
              .clip_id = j,
            };
          }
        }

        if (clip_resize.should_resize) {
          break;
        }
      }
    }

    tl_state_.resize_clips.push_back(clip_resize);
  }

  if (left) {
    double max_pos = DBL_MAX;

    for (int32_t track_id = first_selected_track; const auto& clip : tl_state_.resize_clips) {
      Track* track = Engine2::tracks[track_id];
      if (clip.should_resize) {
        max_pos = math::min(max_pos, track->clips[clip.clip_id]->max_time);
      }
      track_id++;
    }

    tl_state_.tool.min_relative_ofs = max_pos;
  } else {
    double min_pos = 0.0;

    for (int32_t track_id = first_selected_track; const auto& clip : tl_state_.resize_clips) {
      Track* track = Engine2::tracks[track_id];
      if (clip.should_resize) {
        min_pos = math::max(min_pos, track->clips[clip.clip_id]->min_time);
      }
      track_id++;
    }

    tl_state_.tool.min_relative_ofs = min_pos;
  }
}

void timeline_draw_track_lanes(const ImVec2& display_size, const ImVec2& view_size) {
  ImFontBaked* font_baked = font_->GetFontBaked(font_size_);
  ImTextureRef font_tex_ref = ImGui::GetIO().Fonts->TexRef;
  const GridProperties grid_props = get_grid_properties(grid_mode_);
  const double scroll_pos_x = timeline_get_scroll_pos_x();
  const double inv_view_scale = 1.0 / view_scale_;
  const float offset_y = vscroll_ + view_pos_.y;
  float track_stack_offset = view_pos_.y - vscroll_;

  layer1_dl_->_ResetForNewFrame();
  layer2_dl_->_ResetForNewFrame();
  layer3_dl_->_ResetForNewFrame();
  layer1_dl_->PushTexture(font_tex_ref);
  layer2_dl_->PushTexture(font_tex_ref);
  layer3_dl_->PushTexture(font_tex_ref);
  layer1_dl_->PushClipRect(view_min_, view_max_);
  layer2_dl_->PushClipRect(view_min_, view_max_);
  layer3_dl_->PushClipRect(view_min_, view_max_);
  clip_draw_buffer.resize(0);
  waveform_cmd1.resize(0);
  waveform_cmd2.resize(0);

  const bool move_or_shift = any_of(tl_state_.type, TimelineState::Move, TimelineState::Shift);
  const bool is_resizing = any_of(tl_state_.type, TimelineState::Resize, TimelineState::Stretch, TimelineState::Nudge);
  const double beat_duration = Engine2::get_beat_duration();
  const double sample_scale = view_scale_ * beat_duration;
  const ImU32 track_line_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.85f).to_uint32();
  const ImU32 label_line = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.45f).to_uint32();

  const bool is_region_selected = tl_state_.select.is_selected;
  const bool show_selected_region = tl_state_.type == TimelineState::Select || is_region_selected;
  double selection_start_pos = tl_state_.select.start_pos;
  double selection_end_pos = tl_state_.select.end_pos;
  int32_t first_selected_track = tl_state_.select.first_track_id;
  int32_t last_selected_track = tl_state_.select.last_track_id;

  if (show_selected_region) {
    // Flip the range if it is opposite
    if (selection_start_pos > selection_end_pos) {
      std::swap(selection_start_pos, selection_end_pos);
    }
    if (first_selected_track > last_selected_track) {
      std::swap(first_selected_track, last_selected_track);
    }
  }

  draw_musical_guidestripes(layer1_dl_, view_min_, display_size, scroll_pos_x, view_scale_);
  draw_musical_grid(layer1_dl_, view_min_, display_size, scroll_pos_x, inv_view_scale, grid_props, 1.0f, false);

  int32_t track_move_offset = 0;
  double relative_move_offset = 0;

  if (tl_state_.select.is_selected) {
    if (any_of(tl_state_.type, TimelineState::Move, TimelineState::Duplicate, TimelineState::Shift)) {
      const double hovered_pos = timeline_get_hovered_position();
      const double relative_offset = hovered_pos - tl_state_.initial_pos;
      int32_t first_track = first_selected_track;
      double shift_amount = 0.0;

      if (any_of(tl_state_.type, TimelineState::Move, TimelineState::Duplicate)) {
        int32_t track_size = (int32_t)Engine2::tracks.size();
        int32_t src_track = tl_state_.tool.initial_track_id;
        int32_t min_move = src_track - first_selected_track;
        int32_t max_move = track_size - (last_selected_track - src_track) - 1;
        track_move_offset = math::clamp(hovered_track_id_.value(), min_move, max_move) - src_track;
        first_track = first_track + track_move_offset;
        relative_move_offset = math::max(relative_offset, tl_state_.tool.min_relative_ofs);
      } else {
        shift_amount = relative_offset;
      }

      for (int32_t i = first_selected_track; i <= last_selected_track; i++) {
        Track* src_track = Engine2::tracks[i];
        Track* dst_track = Engine2::tracks[i + track_move_offset];
        const float height = dst_track->get_height();
        const float track_pos_y = track_stack_[i + track_move_offset] + view_min_.y - vscroll_;
        const float track_view_min_y = view_min_.y - height - track_separator_height_;

        if (track_pos_y > view_max_.y) {
          break;
        }

        const ClipSpan& selected_region = tl_state_.selected_track_clips[i - first_selected_track];
        if (track_pos_y < track_view_min_y || !selected_region.contains_clip) {
          continue;
        }

        for (uint32_t j = selected_region.first; j <= selected_region.last; j++) {
          Clip* clip = src_track->clips[j];
          double start_pos = clip->min_time;
          double end_pos = clip->max_time;
          double start_offset = clip->start_offset;
          const bool is_audio = clip->is_audio();
          const double speed = is_audio ? clip->audio.speed : 1.0;
          const double sample_rate = clip->get_asset_sample_rate();
          bool left_side_partially_selected = selected_region.left_side_partially_selected(j);
          bool right_side_partially_selected = selected_region.right_side_partially_selected(j);

          if (left_side_partially_selected && right_side_partially_selected) {
            const double new_start_pos = start_pos + selected_region.first_offset;
            const double start_pos_moved = new_start_pos + relative_move_offset;
            const double length = (end_pos - new_start_pos) + selected_region.last_offset;
            const double end_pos_moved = start_pos_moved + length;
            const double new_start_ofs =
                shift_clip_content(clip, -selected_region.first_offset + shift_amount, beat_duration);
            start_pos = start_pos_moved;
            end_pos = end_pos_moved;
            start_offset = new_start_ofs;
          } else if (right_side_partially_selected) {
            const double new_start_pos = start_pos + selected_region.first_offset;
            const double start_pos_moved = new_start_pos + relative_move_offset;
            const double end_pos_moved = start_pos_moved + (end_pos - new_start_pos);
            const double new_start_ofs =
                shift_clip_content(clip, -selected_region.first_offset + shift_amount, beat_duration);
            start_pos = start_pos_moved;
            end_pos = end_pos_moved;
            start_offset = new_start_ofs;
          } else if (left_side_partially_selected) {
            const double new_max_time = end_pos + selected_region.last_offset;
            const double start_pos_moved = start_pos + relative_move_offset;
            const double end_pos_moved = start_pos_moved + (new_max_time - start_pos);

            start_pos = start_pos_moved;
            end_pos = end_pos_moved;

            if (shift_amount != 0.0) {
              start_offset = shift_clip_content(clip, shift_amount, beat_duration);
            }
          } else [[likely]] {
            const auto [new_start_pos, new_end_pos] = calc_move_clip(clip, relative_move_offset, 0.0);
            start_pos = new_start_pos;
            end_pos = new_end_pos;

            if (shift_amount != 0.0) {
              start_offset = shift_clip_content(clip, shift_amount, beat_duration);
            }
          }

          timeline_add_clip_draw_data(
              clip, inv_view_scale, start_pos, end_pos, start_offset, speed, track_pos_y, height, ClipDrawCmd2::Topmost);
        }
      }
    } else if (is_resizing) {
      const double hovered_pos = timeline_get_hovered_position();
      const double relative_offset = hovered_pos - tl_state_.initial_pos;
      const double resize_limit = tl_state_.tool.min_relative_ofs;
      const double min_length = 1.0 / get_grid_division(inv_view_scale, grid_mode_, triplet_);
      const ClipResizeMode mode = tl_state_.get_resize_mode();

      for (int32_t i = first_selected_track; i <= last_selected_track; i++) {
        Track* track = Engine2::tracks[i];
        const float height = track->get_height();
        const float track_pos_y = track_stack_[i] + view_min_.y - vscroll_;
        const float track_view_min_y = view_min_.y - height - track_separator_height_;

        if (track_pos_y > view_max_.y) {
          break;
        }

        const TrackClipResizeInfo& resize_clip = tl_state_.resize_clips[i - first_selected_track];
        if (track_pos_y < track_view_min_y || !resize_clip.should_resize) {
          continue;
        }

        Clip* clip = track->clips[resize_clip.clip_id];
        double start_offset = clip->start_offset;
        const auto [new_start_pos, new_end_pos, new_start_offset, new_speed] =
            calc_resize_clip(clip, relative_offset, min_length, resize_limit, beat_duration, tl_state_.tool.left_side, mode);
        timeline_add_clip_draw_data(
            clip,
            inv_view_scale,
            new_start_pos,
            new_end_pos,
            new_start_offset,
            new_speed,
            track_pos_y,
            height,
            ClipDrawCmd2::Topmost);
      }
    }
  } else {
    const double hovered_pos = timeline_get_hovered_position();
    const double relative_offset = hovered_pos - tl_state_.initial_pos;

    switch (tl_state_.type) {
      case TimelineState::Move:
      case TimelineState::Duplicate: {
        int32_t src_track = tl_state_.tool.initial_track_id;
        int32_t dst_track = hovered_track_id_.value();
        Track* track = Engine2::tracks[src_track];
        Clip* clip = track->clips[tl_state_.tool.clip_id];
        double start_offset = clip->start_offset;
        float track_pos_y = track_stack_[dst_track] + view_min_.y - vscroll_;
        const double speed = clip->is_audio() ? clip->audio.speed : 1.0;
        const auto [start_pos, end_pos] = calc_move_clip(clip, relative_offset, 0.0);

        timeline_add_clip_draw_data(
            clip,
            inv_view_scale,
            start_pos,
            end_pos,
            start_offset,
            speed,
            track_pos_y,
            track->get_height(),
            ClipDrawCmd2::Topmost);
        break;
      }
      case TimelineState::Shift: {
        Track* track = Engine2::tracks[tl_state_.tool.initial_track_id];
        Clip* clip = track->clips[tl_state_.tool.clip_id];
        float track_pos_y = track_stack_[tl_state_.tool.initial_track_id] + view_min_.y - vscroll_;
        const double speed = clip->is_audio() ? clip->audio.speed : 1.0;
        double start_offset = shift_clip_content(clip, relative_offset, beat_duration);

        timeline_add_clip_draw_data(
            clip,
            inv_view_scale,
            clip->min_time,
            clip->max_time,
            start_offset,
            speed,
            track_pos_y,
            track->get_height(),
            ClipDrawCmd2::Topmost);
        break;
      }
      case TimelineState::Resize:
      case TimelineState::Stretch:
      case TimelineState::Nudge: {
        const ClipResizeMode mode = tl_state_.get_resize_mode();
        bool left_side = tl_state_.tool.left_side;
        Track* track = Engine2::tracks[tl_state_.tool.initial_track_id];
        Clip* clip = track->clips[tl_state_.tool.clip_id];
        float track_pos_y = track_stack_[tl_state_.tool.initial_track_id] + view_min_.y - vscroll_;
        const double min_length = 1.0 / get_grid_division(inv_view_scale, grid_mode_, triplet_);
        const auto [new_start_pos, new_end_pos, new_start_offset, new_speed] = calc_resize_clip(
            clip,
            relative_offset,
            min_length,
            left_side ? clip->max_time : clip->min_time,
            beat_duration,
            tl_state_.tool.left_side,
            mode);

        timeline_add_clip_draw_data(
            clip,
            inv_view_scale,
            new_start_pos,
            new_end_pos,
            new_start_offset,
            new_speed,
            track_pos_y,
            track->get_height(),
            ClipDrawCmd2::Topmost);
        break;
      }
      default: break;
    }
  }

  for (int32_t i = first_visible_track_; i < last_visible_track_; i++) {
    Track* track = Engine2::tracks[i];
    const float height = track->get_height();
    const float track_pos_abs_y = track_stack_[i] + view_min_.y - vscroll_;
    const bool mini_clip = height <= 30.0f;
    ClipSpan* selected_clip_span = nullptr;
    TrackClipResizeInfo* resize_clip = nullptr;

    if (is_region_selected && i >= first_selected_track && i <= last_selected_track) {
      selected_clip_span = &tl_state_.selected_track_clips[i - first_selected_track];
      if (is_resizing) {
        resize_clip = &tl_state_.resize_clips[i - first_selected_track];
      }
    }

    for (uint32_t j = 0; j < track->clips.size(); j++) {
      Clip* clip = track->clips[j];
      double start_pos = clip->min_time;
      double end_pos = clip->max_time;
      double start_offset = clip->start_offset;
      const double speed = clip->get_audio_speed();
      const bool is_selected_clip = selected_clip_ && selected_clip_->first == i && selected_clip_->second == j;
      uint32_t clip_draw_flags = is_selected_clip ? ClipDrawCmd2::Highlighted : 0;

      if (selected_clip_span) {
        if (move_or_shift) {
          if (math::in_range_inclusive(j, selected_clip_span->first, selected_clip_span->last)) {
            const bool left_side_partially_selected = selected_clip_span->left_side_partially_selected(j);
            const bool right_side_partially_selected = selected_clip_span->right_side_partially_selected(j);
            const bool is_audio = clip->is_audio();
            const double sample_rate = clip->get_asset_sample_rate();

            if (left_side_partially_selected && right_side_partially_selected) {
              const double shift_amount = start_pos - selection_end_pos;
              const double rhs_start_ofs = shift_clip_content(clip, shift_amount, beat_duration);
              timeline_add_clip_draw_data(
                  clip,
                  inv_view_scale,
                  start_pos,
                  selection_start_pos,
                  start_offset,
                  speed,
                  track_pos_abs_y,
                  height,
                  clip_draw_flags);
              timeline_add_clip_draw_data(
                  clip, inv_view_scale, selection_end_pos, end_pos, rhs_start_ofs, speed, track_pos_abs_y, height, 0);
              continue;
            } else if (left_side_partially_selected) {
              const double shift_amount = start_pos - selection_end_pos;
              const double rhs_start_ofs = shift_clip_content(clip, shift_amount, beat_duration);
              timeline_add_clip_draw_data(
                  clip,
                  inv_view_scale,
                  selection_end_pos,
                  end_pos,
                  rhs_start_ofs,
                  speed,
                  track_pos_abs_y,
                  height,
                  clip_draw_flags);
              continue;
            } else if (right_side_partially_selected) {
              timeline_add_clip_draw_data(
                  clip,
                  inv_view_scale,
                  start_pos,
                  selection_start_pos,
                  start_offset,
                  speed,
                  track_pos_abs_y,
                  height,
                  clip_draw_flags);
              continue;
            } else {
              continue;
            }
          }
        } else if (is_resizing && resize_clip->clip_id == j && resize_clip->should_resize) {
          continue;
        }
      } else {
        switch (tl_state_.type) {
          case TimelineState::Move:
          case TimelineState::Shift:
            if (tl_state_.tool.initial_track_id == i && tl_state_.tool.clip_id == j) {
              continue;
            }
            break;
          case TimelineState::Duplicate: break;
          case TimelineState::Resize:
          case TimelineState::Stretch:
          case TimelineState::Nudge:
            if (tl_state_.tool.initial_track_id == i && tl_state_.tool.clip_id == j) {
              continue;
            }
            break;
          default: break;
        }
      }

      if (!timeline_add_clip_draw_data(
              clip, inv_view_scale, start_pos, end_pos, start_offset, speed, track_pos_abs_y, height, clip_draw_flags)) {
        break;
      }
    }

    if (show_selected_region &&
        math::in_range_inclusive(i, first_selected_track + track_move_offset, last_selected_track + track_move_offset)) {
      // static const ImU32 selection_range_fill = ImColor(28, 150, 237, 90);
      // static const ImU32 selection_range_border = ImColor(28, 150, 237, 255);
      static const ImU32 selection_range_fill = ImColor(115, 165, 230, 86);
      static const ImU32 selection_range_border = ImColor(28, 150, 237, 255);
      double x0 = math::round((selection_start_pos + relative_move_offset) * inv_view_scale);
      double x1 = math::round((selection_end_pos + relative_move_offset) * inv_view_scale);
      const ImVec2 min_bb((float)(scroll_offset_x_ + x0), track_pos_abs_y);
      const ImVec2 max_bb((float)(scroll_offset_x_ + x1), track_pos_abs_y + height);
      layer3_dl_->AddRectFilled(min_bb, max_bb, selection_range_fill);
      layer3_dl_->AddLine(min_bb, ImVec2(min_bb.x, max_bb.y), selection_range_border);
      layer3_dl_->AddLine(ImVec2(max_bb.x, min_bb.y), max_bb, selection_range_border);
    }

    if (tl_state_.type == TimelineState::DragDropFiles) {
      if (!tl_state_.drag_drop_files.item_dropped && tl_state_.drag_drop_files.first_track_id == i) {
        BrowserFilePayload* payload_data = tl_state_.drag_drop_files.payload_data;
        double length = 1.0;

        if (payload_data->type == BrowserItem::Sample) {
          length = samples_to_beat(payload_data->content_length, payload_data->sample_rate, beat_duration);
        }

        const double highlight_pos = tl_state_.drag_drop_files.position;
        const double x0 = highlight_pos * inv_view_scale;
        const double x1 = (highlight_pos + length) * inv_view_scale;
        const ImVec2 min_bb((float)(scroll_offset_x_ + x0), track_pos_abs_y);
        const ImVec2 max_bb((float)(scroll_offset_x_ + x1), track_pos_abs_y + height);
        std::string_view filename = tl_state_.drag_drop_files.payload_data->filename;
        layer3_dl_->AddRectFilled(min_bb, max_bb, highlight_color_);
        timeline_draw_clip_label(layer3_dl_, filename, mini_clip, height, min_bb, max_bb);
      }
    }

    float next_track_pos_y = track_pos_abs_y + height;
    // Divisor line
    im_draw_hline(layer1_dl_, next_track_pos_y + 0.5f, view_min_.x, view_max_.x, track_line_color);
  }

  for (const auto& clip : clip_draw_buffer) {
    const double x0 = clip.start_pos_x;
    const double x1 = clip.end_pos_x;
    const float height = clip.height;
    const float x0_clipped = math::max((float)math::round(x0), view_min_.x - 3.0f);
    const float x1_clipped = math::min((float)math::round(x1) - 0.5f, view_max_.x + 3.0f);
    const float clip_label_max_y = clip.pos_y + font_size_ + 5.0f;

    const ImVec2 clip_label_min_bb(x0_clipped, clip.pos_y);
    const ImVec2 clip_label_max_bb(x1_clipped, clip_label_max_y);
    const ImVec2 clip_content_min(x0_clipped, clip_label_max_y);
    const ImVec2 clip_content_max(x1_clipped, clip.pos_y + height);

    const Color color(clip.color);
    const Color bg_color = color.darken(0.15f);
    const Color content_color = color.brighten(1.4f);
    const ColorU32 bg_color_u32 = bg_color.to_uint32();
    const ColorU32 label_color_u32 = color.to_uint32();
    const ColorU32 content_color_u32 = content_color.to_uint32();

    const bool mini_clip = height <= 30.0f;
    const bool topmost = has_bit(clip.flags, ClipDrawCmd2::Topmost);
    const bool highlighted = has_bit(clip.flags, ClipDrawCmd2::Highlighted);
    auto* dl = !topmost ? layer1_dl_ : layer2_dl_;

    if (topmost) {
      dl->AddRect(clip_label_min_bb, clip_content_max, 0x3F000000, 3.0f, ImDrawFlags_RoundCornersTop, 4.5f);
    }

    dl->AddRectFilled(clip_label_min_bb, clip_label_max_bb, label_color_u32, 3.0f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(clip_content_min, clip_content_max, bg_color_u32, 0.5f, ImDrawFlags_RoundCornersBottom);

    if (!mini_clip) {
      const ColorU32 line_color_u32 = color.change_alpha(color.a * 0.80f).premult_alpha().to_uint32();
      im_draw_hline(dl, clip_label_max_bb.y - 0.5f, clip_content_min.x, clip_content_max.x, line_color_u32);
    }

    if (highlighted) {
      dl->AddRect(clip_label_min_bb, clip_content_max, content_color_u32, 3.0f, ImDrawFlags_RoundCornersTop);
    }

    ClipType type = clip.type;

    switch (type) {
      case ClipType::Audio: {
        WaveformVisual* waveform = clip.audio.waveform;

        if (!waveform || mini_clip) {
          break;
        }

        static constexpr double log_base4 = 1.0 / 1.3862943611198906;  // 1.0 / log(4.0)
        const double scale_x = sample_scale * (double)waveform->sample_rate * clip.audio.speed;
        const double inv_scale_x = 1.0 / scale_x;
        const double mip_index = std::log(scale_x * 0.5) * log_base4;  // Scale -> Index
        const int32_t index = math::clamp((int32_t)mip_index, 0, waveform->mipmap_count - 1);
        const double mip_scale = std::pow(4.0, mip_index - (double)index) * 2.0;  // Index -> Mip Scale
        // const double mip_index = (std::log(scale_x * 0.5) * log_base4) * 0.5; // Scale -> Index
        // const int32_t index = math::clamp((int32_t)mip_index, 0, sample_peaks->mipmap_count - 1);
        // const double mult = std::pow(4.0, (double)index - 1.0);
        // const double mip_scale =
        //     std::pow(4.0, 2.0 * (mip_index - (double)index)) * 8.0 * mult; // Index -> Mip Scale
        // const double mip_div = math::round(scale_x / mip_scale);

        const double waveform_len = ((double)waveform->sample_count - clip.start_offset) * inv_scale_x;
        const double rel_min_x = x0 - (double)view_min_.x;
        const double rel_max_x = x1 - (double)view_min_.x;
        const double min_pos_x = math::max(rel_min_x, 0.0);
        const double max_pos_x = math::min(math::min(rel_max_x, rel_min_x + waveform_len), (double)(view_size.x + 2.0));
        const double draw_count = math::max(max_pos_x - min_pos_x, 0.0);
        const double length = rel_max_x - rel_min_x;
        const float gap_size = (float)(length / std::floor(length));

        // Log::debug("{} {} {}", index, mip_scale, (double)sample_peaks->sample_count / mip_index);
        /*Log::debug("{} {} {} {} {}", sample_peaks->sample_count / (size_t)mip_div, mip_div, index,
                   math::round(start_offset / mip_div), mip_scale);*/

        if (draw_count) {
          auto& waveform_cmd_list = !topmost ? waveform_cmd1 : waveform_cmd2;
          double waveform_start = clip.start_offset * inv_scale_x;
          const double start_idx = std::round(math::max(-rel_min_x, 0.0) + waveform_start);
          const float min_bb_x = (float)math::round(min_pos_x);
          const float max_bb_x = (float)math::round(max_pos_x);
          const float pos_y = clip_content_min.y - offset_y;
          if (waveform->channels == 2) {
            const float height = std::floor((clip_content_max.y - clip_content_min.y) * 0.5f);
            waveform_cmd_list.push_back({
              .waveform_vis = waveform,
              .min_x = min_bb_x,
              .min_y = pos_y,
              .max_x = max_bb_x,
              .max_y = pos_y + height,
              .gain = clip.gain,
              .scale_x = (float)mip_scale,
              .gap_size = gap_size,
              .color = content_color_u32,
              .mip_index = index,
              .channel = 0,
              .start_idx = (uint32_t)start_idx,
              .draw_count = (uint32_t)draw_count + 2,
            });
            waveform_cmd_list.push_back({
              .waveform_vis = waveform,
              .min_x = min_bb_x,
              .min_y = pos_y + height,
              .max_x = max_bb_x,
              .max_y = pos_y + height * 2.0f,
              .gain = clip.gain,
              .scale_x = (float)mip_scale,
              .gap_size = gap_size,
              .color = content_color_u32,
              .mip_index = index,
              .channel = 1,
              .start_idx = (uint32_t)start_idx,
              .draw_count = (uint32_t)draw_count + 2,
            });
          } else {
            waveform_cmd_list.push_back({
              .waveform_vis = waveform,
              .min_x = min_bb_x,
              .min_y = pos_y,
              .max_x = max_bb_x,
              .max_y = clip_content_max.y - offset_y,
              .gain = clip.gain,
              .scale_x = (float)mip_scale,
              .gap_size = gap_size,
              .color = content_color_u32,
              .mip_index = index,
              .start_idx = (uint32_t)start_idx,
              .draw_count = (uint32_t)draw_count + 2,
            });
          }
        }
        break;
      }
      case ClipType::Midi: {
        constexpr float min_note_size_px = 2.5f;
        constexpr float max_note_size_px = 10.0f;
        constexpr uint32_t min_note_range = 4;
        const MidiData* data = clip.midi.data;
        if (data) {
          const uint32_t min_note = data->min_note;
          const uint32_t max_note = data->max_note;
          uint32_t note_range = (data->max_note + 1) - min_note;

          if (note_range < min_note_range)
            note_range = 13;

          const float content_height = clip_content_max.y - clip_content_min.y;
          const float note_height = content_height / (float)note_range;
          float max_note_size = math::min(note_height, max_note_size_px);
          const float min_note_size = math::max(max_note_size, min_note_size_px);
          const float offset_y = clip_content_min.y + ((content_height * 0.5f) - (max_note_size * note_range * 0.5f));

          // Fix note overflow
          if (content_height < math::round(min_note_size * note_range)) {
            max_note_size = (content_height - 2.0f) / (float)(note_range - 1u);
          }

          const float min_view = math::max(x0_clipped, view_min_.x);
          const float max_view = math::min(x1_clipped, view_max_.x);
          const double note_scale = inv_view_scale / (double)clip.midi.rate;
          const ColorU32 note_color = content_color.change_alpha(!mini_clip ? 1.0f : 0.20f).to_uint32();

          double min_start_x = x0 - clip.start_offset * note_scale;
          for (uint32_t j = 0; const auto& note : data->note_sequence) {
            float min_pos_x = (float)math::round(min_start_x + note.min_time * note_scale);
            float max_pos_x = (float)math::round(min_start_x + note.max_time * note_scale);

            if (max_pos_x < min_view)
              continue;
            if (min_pos_x >= max_view)
              break;

            const float pos_y = offset_y + (float)(max_note - note.key) * max_note_size;
            min_pos_x = math::max(min_pos_x, min_view);
            max_pos_x = math::min(max_pos_x, max_view);

            if (min_pos_x >= max_pos_x)
              continue;

            const ImVec2 a(min_pos_x + 0.5f, pos_y);
            const ImVec2 b(max_pos_x, pos_y + min_note_size - 0.5f);

#if DEBUG_MIDI_CLIPS == 1
            char c[32]{};
            fmt::format_to_n(c, std::size(c), "ID: {}", j);
            layer2_draw_list->AddText(a - ImVec2(0.0f, 13.0f), 0xFFFFFFFF, c);
            j++;
#endif
            dl->PathLineTo(a);
            dl->PathLineTo(ImVec2(b.x, a.y));
            dl->PathLineTo(b);
            dl->PathLineTo(ImVec2(a.x, b.y));
            dl->PathFillConvex(note_color);
          }
        }
        break;
      }
      default: break;
    }

    if (clip.name_len != 0) {
      timeline_draw_clip_label(dl, clip.name, mini_clip, height, clip_label_min_bb, clip_label_max_bb);
    }
  }

  layer3_dl_->PopClipRect();
  layer2_dl_->PopClipRect();
  layer1_dl_->PopClipRect();
  layer3_dl_->PopTexture();
  layer2_dl_->PopTexture();
  layer1_dl_->PopTexture();

  // ImGui::UpdateTexturesEndFrame();

  ImVec2 fb_scale = current_fb_scale_;
  ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();
  g_renderer->begin_render(timeline_fb_, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

  layer_draw_data_.Clear();
  layer_draw_data_.DisplayPos = view_min_;
  layer_draw_data_.DisplaySize = display_size;
  layer_draw_data_.FramebufferScale = fb_scale;
  layer_draw_data_.OwnerViewport = owner_viewport;
  layer_draw_data_.AddDrawList(layer1_dl_);
  g_renderer->render_imgui_draw_data(&layer_draw_data_);
  gfx_draw_waveform_batch(waveform_cmd1, fb_scale.x, fb_scale.y, 0, 0, display_size.x, display_size.y);

  layer_draw_data_.Clear();
  layer_draw_data_.DisplayPos = view_min_;
  layer_draw_data_.DisplaySize = display_size;
  layer_draw_data_.FramebufferScale = fb_scale;
  layer_draw_data_.OwnerViewport = owner_viewport;
  layer_draw_data_.AddDrawList(layer2_dl_);
  g_renderer->render_imgui_draw_data(&layer_draw_data_);
  gfx_draw_waveform_batch(waveform_cmd2, fb_scale.x, fb_scale.y, 0, 0, display_size.x, display_size.y);

  layer_draw_data_.Clear();
  layer_draw_data_.DisplayPos = view_min_;
  layer_draw_data_.DisplaySize = display_size;
  layer_draw_data_.FramebufferScale = fb_scale;
  layer_draw_data_.OwnerViewport = owner_viewport;
  layer_draw_data_.AddDrawList(layer3_dl_);
  g_renderer->render_imgui_draw_data(&layer_draw_data_);

  g_renderer->end_render();
}

void timeline_update_track_stack(int32_t track_index, int32_t num_tracks) {
  if (num_tracks == -1) {
    num_tracks = (int32_t)Engine2::tracks.size();
  }

  track_stack_.resize(num_tracks);

  float pos_y = track_stack_[track_index];
  for (int32_t i = track_index; i < num_tracks; i++) {
    Track* track = Engine2::tracks[i];
    track_stack_[i] = pos_y;
    pos_y += track->get_height() + track_separator_height_;
  }
}

void timeline_query_selected_range() {
  int32_t first = tl_state_.select.first_track_id;
  int32_t last = tl_state_.select.last_track_id;
  double start_pos = tl_state_.select.start_pos;
  double end_pos = tl_state_.select.end_pos;
  bool contains_clip = false;

  tl_state_.selected_track_clips.clear();
  tl_state_.selected_track_clips.reserve(last - first + 1);
  Log::debug("Selected tracks:");

  for (int32_t i = first; i <= last; i++) {
    Track* track = Engine2::tracks[i];
    auto query_result = track->query_clip_by_range2(start_pos, end_pos);
    if (query_result) {
      contains_clip = true;
      tl_state_.selected_track_clips.push_back(query_result);
      Log::debug(
          "Track {}: {} {} {} {}",
          i,
          query_result.first,
          query_result.last,
          query_result.first_offset,
          query_result.last_offset);
    } else {
      tl_state_.selected_track_clips.emplace_back();  // Insert empty result
    }
  }
}

void timeline_add_track() {
  float hue_index = (float)track_color_spin_ / 15.0f;
  // float sat_amount = 0.15f * std::fmod(1.0f - 2.0f * hue_index, 1.0f) + 0.85f;
  CmdAddTrack* cmd = new CmdAddTrack();
  cmd->name = "New track";
  cmd->color = Color::from_hsluv(hue_index, 0.68f, 0.5043f);
  // cmd->color = Color::from_hsluv(251.4 / 360.0f, 0.724f, 0.487f);
  // cmd->color = Color::from_hsluv(hue_index, 0.68f, 0.5343f);
  //   cmd->color = Color::from_hsv(hue_index, 0.6321f, 0.90f);
  //   cmd->color = Color::from_hsv((float)track_color_spin_ / 15.0f, 0.6172f, 0.80f);

  CommandManager2::execute_command("Add track", cmd);
  track_color_spin_ = (track_color_spin_ + 1) % 15;
  force_redraw_ = true;
}

void timeline_add_midi_clips() {
  if (tl_state_.select.is_selected) {
    CmdAddMidiClips* cmd = new CmdAddMidiClips();
    cmd->first_track = tl_state_.select.first_track_id;
    cmd->last_track = tl_state_.select.last_track_id;
    cmd->start_pos = tl_state_.select.start_pos;
    cmd->end_pos = tl_state_.select.end_pos;
    CommandManager2::execute_command("Add MIDI clips", cmd);
    tl_state_.clear_selection();
  }
}

void timeline_add_clip_from_file() {
  CmdAddClipFromFile* cmd = new CmdAddClipFromFile();
  cmd->track_id = tl_state_.drag_drop_files.first_track_id;
  cmd->position = tl_state_.drag_drop_files.position;
  cmd->file_path = tl_state_.drag_drop_files.payload_data->path;
  CommandManager2::execute_command("Add clip", cmd);
  tl_state_.clear_selection();
}

bool timeline_delete_region() {
  if (tl_state_.select.is_selected) {
    timeline_query_selected_range();
    CmdDeleteClips* cmd = new CmdDeleteClips();
    cmd->clip_spans = tl_state_.selected_track_clips;
    cmd->first_track = tl_state_.select.first_track_id;
    cmd->start_pos = tl_state_.select.start_pos;
    cmd->end_pos = tl_state_.select.end_pos;
    CommandManager2::execute_command("Delete selected region", cmd);
    tl_state_.clear_selection();
    force_redraw_ = redraw_ = true;
    return true;
  }
  return false;
}

}  // namespace wb
