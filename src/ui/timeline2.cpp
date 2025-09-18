#include "timeline2.h"

#include <imgui.h>

#include <algorithm>

#include "browser.h"
#include "context_menu.h"
#include "controls.h"
#include "engine/clip_command.h"
#include "engine/command_manager2.h"
#include "engine/engine2.h"
#include "engine/track.h"
#include "engine/track_command.h"
#include "font.h"
#include "gfx/draw.h"
#include "gfx/renderer.h"
#include "grid.h"
#include "layout.h"
#include "plugins.h"
#include "timeline_controls.h"

namespace wb {

struct TimelineDragDropFilesState {
  BrowserFilePayload* payload_data;
  double position;
  int32_t first_track_id;
  bool item_dropped;
};

struct TimelineMoveState {
  double relative_pos;
};

struct TimelineState {
  enum {
    None,
    Select,
    DragDropFiles,
    Move,
  };

  uint32_t type;
  Vector<ClipQueryResult2> selected_track_clips;

  struct {
    bool is_selected;
    double start_pos;
    double end_pos;
    int32_t first_track_id;
    int32_t last_track_id;
  } select;

  union {
    TimelineDragDropFilesState drag_drop_files;
    TimelineMoveState move;
  };

  void clear_selection() {
    selected_track_clips.resize(0);
    select = {};
  }

  void end_action() {
    switch (type) {
      case None:
      case Select: break;
      case DragDropFiles: drag_drop_files = {}; break;
      case Move: move = {}; break;
    }
    type = None;
  }
};

static constexpr uint32_t timeline_mouse_btn_flags_ =
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle;
static constexpr float zoom_sensitivity_ = 0.12f;
static constexpr float track_separator_height_ = 2.0f;
static constexpr uint32_t playhead_color_ = 0xE553A3F9;
static constexpr uint32_t highlight_color_ = 0x9F555555;

static TimelineViewState view_state_;
static double view_scale_;
static double scroll_offset_x_;
static double max_length_ = 100.0;
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
static ImVec2 timeline_layout_size_;
static ImVec2 timeline_display_size_{ 16.0f, 16.0f };
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
static float first_visible_track_pos_y_;
static int32_t first_visible_track_;
static int32_t last_visible_track_;
static TimelineState timeline_state_;
static std::optional<int32_t> hovered_track_id_;

static Vector<WaveformDrawCmd> waveform_cmd;

static Track* context_menu_track_{};
static uint32_t context_menu_track_id_{};
static Clip* context_menu_clip_{};
static Color tmp_color_;
static std::string tmp_name_;

bool g_timeline2_window_open = true;

static void timeline_render_navbar();
static void timeline_render_track_panel();
static void timeline_render_track_lanes();
static void timeline_handle_mouse_event();
static void timeline_handle_track_event();
static void timeline_query_selected_range();
static void timeline_add_track();
static void timeline_add_clip_from_file();

inline static double timeline_get_view_scale() {
  return view_state_.get_view_scale(max_length_, timeline_display_size_.x);
}

inline static double timeline_get_scroll_pos_x() {
  return math::round((view_state_.start * max_length_) / view_scale_);
}

inline static double timeline_get_hovered_position() {
  double position = ((double)(mouse_pos_.x - view_pos_.x) * view_scale_ + view_state_.start * max_length_);
  return math::max(round_to_grid(position, 1.0 / view_scale_, grid_mode_, triplet_), 0.0);
}

inline static void timeline_hscroll(float scroll_delta) {
  if (view_state_.scroll(scroll_delta, max_length_, -view_scale_)) {
    redraw_ = true;
  }
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
  const float label_padding_y = mini_clip ? (height - font_size_) * 0.5f : 2.0f;
  const ImVec2 label_pos(std::max(label_min.x, view_min_.x) + label_padding_x, label_min.y + label_padding_y);
  const ImVec4 clip_label_rect(label_min.x, label_min.y, label_max.x - 6.0f, label_max.y);
  dl->AddText(font_, font_size_, label_pos, text_col, str, str + str_size, 0.0f, &clip_label_rect);
}

void timeline_init() {
  Engine2::add_bpm_update_listener(nullptr, [](void* userdata, double beat_duration, double bpm) { force_redraw_ = true; });
  CommandManager2::add_cmd_history_update_listener(nullptr, [](void* userdata) { force_redraw_ = true; });
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
    ImGui::PopStyleVar();
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

  ImGui::PopStyleVar();
  timeline_render_navbar();

  timeline_layout_size_ = ImGui::GetContentRegionAvail();
  layout::begin_columns("tl_cols", 2, timeline_layout_size_);
  {
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

    layout::next_column(track_panel_width_, 100.0f);
    timeline_render_track_panel();
    layout::next_column(0.0f);
    timeline_render_track_lanes();
  }
  layout::end_columns();

  controls::end_window();
}

void timeline_render_navbar() {
  static float separator_size = 2.0f;
  ImVec2 window_size = ImGui::GetContentRegionAvail();
  float timeline_w = timeline_display_size_.x;
  float navbar_w = window_size.x - track_panel_width_;

  ImGui::SetCursorPosX(math::max(track_panel_width_, track_panel_min_width_) + separator_size);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  ImGui::BeginGroup();

  if (controls::timeline_scrollbar("tl_hscroll", navbar_w, 0.1, max_length_, &view_state_)) {
    redraw_ = true;
  }

  double time_pos = Engine2::playhead;
  controls::TimelineRulerResult tr_result =
      controls::timeline_ruler("tl_ruler", grid_mode_, triplet_, timeline_w, max_length_, &time_pos, &view_state_);
  if (tr_result != controls::TimelineRulerResult::None) {
    switch (tr_result) {
      case controls::TimelineRulerResult::Zoom: redraw_ = true; break;
      case controls::TimelineRulerResult::TimePositionChanged: Engine2::set_playhead_position(time_pos); break;
      default: break;
    }
  }

  ImGui::EndGroup();

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  im_draw_hline(
      draw_list,
      cursor_pos.y - 1.0f,
      cursor_pos.x,
      cursor_pos.x + ImGui::GetContentRegionAvail().x,
      ImGui::GetColorU32(ImGuiCol_Separator));

  ImGui::PopStyleVar();
}

void timeline_render_track_panel() {
  static constexpr float vu_meter_width = 11.0f;
  static constexpr float track_color_width = 8.0f;
  static constexpr ImVec2 padding(6.0f, 2.0f);
  static constexpr ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);
  constexpr ImGuiWindowFlags track_control_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                          ImGuiWindowFlags_NoBackground |
                                                          ImGuiWindowFlags_AlwaysUseWindowPadding;

  const bool is_recording = Engine2::is_recording();
  uint32_t num_tracks = Engine2::tracks.size();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 top_left_cursor_pos = ImGui::GetCursorScreenPos();
  const auto& style = ImGui::GetStyle();
  bool open_track_context_menu = false;
  bool move_track = false;
  uint32_t move_track_src = 0;
  uint32_t move_track_dst = 0;

  track_panel_width_ = layout::get_current_column_width();

  for (uint32_t i = 0; i < num_tracks; i++) {
    Track* track = Engine2::tracks[i];
    float height = track->get_height();
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

    if (ImGui::BeginChild("##track_controls", track_controls_size, 0, track_control_window_flags)) {
      float volume = track->ui_parameter_state.volume_db;
      bool mute = track->ui_parameter_state.mute;

      ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);

      if (controls::collapse_button2("##track_collapse", &track->shown)) {
        redraw_ = true;
      }
      ImGui::SameLine(0.0f, 5.0f);

      const char* begin_name_str = track->name.c_str();
      const char* end_name_str = begin_name_str + track->name.size();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
      if (!track->name.empty()) {
        ImGui::TextUnformatted(begin_name_str, end_name_str);
      } else {
        ImGui::BeginDisabled();
        ImGui::TextUnformatted("(unnamed)");
        ImGui::EndDisabled();
      }
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);

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
          constexpr ImGuiSelectableFlags selected_flags = ImGuiSelectableFlags_Highlight;
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));

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

          ImGui::PopStyleVar();  // ImGuiStyleVar_WindowPadding
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

        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::SmallButton("FX"))
          ImGui::OpenPopup("track_plugin_context_menu");
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

      ImGui::PopStyleVar();  // ImGuiStyleVar_ItemSpacing
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
      redraw_ = true;
    }

    ImGui::PopStyleVar();  // ImGuiStyleVar_ItemSpacing
    ImGui::PopID();
  }

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

  if (ImGui::BeginPopup("track_context_menu")) {
    /*if (track_context_menu(context_menu_track_, context_menu_track_id_, &tmp_name_, &tmp_color_)) {
      redraw_ = true;
    }*/
    ImGui::EndPopup();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  ImGui::BeginChild("track_add", ImVec2(track_panel_width_, 60.0f), 0, track_control_window_flags);
  if (ImGui::Button("+ Track", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    timeline_add_track();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  track_lanes_height_ = ImGui::GetCursorPos().y;
  // Log::debug("{} {}", ImGui::GetCursorPos().y, track_lanes_height_);
}

void timeline_render_track_lanes() {
  ImVec2 available_size = ImGui::GetContentRegionAvail();
  const float view_width = layout::get_current_column_width();

  view_pos_ = ImGui::GetCursorScreenPos();
  view_min_ = ImVec2(view_pos_.x, vscroll_ + view_pos_.y);
  view_max_ = ImVec2(view_pos_.x + available_size.x, vscroll_ + view_pos_.y + timeline_layout_size_.y);

  ImVec2 view_size(available_size.x, math::max(track_lanes_height_, timeline_layout_size_.y));
  ImVec2 display_size = view_max_ - view_min_;
  const float offset_y = vscroll_ + view_pos_.y;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  ImGui::InvisibleButton("##timeline_view", view_size, timeline_mouse_btn_flags_);
  timeline_hovered_ = ImGui::IsItemHovered();
  ImGui::PopStyleVar();

  if (display_size.x != timeline_display_size_.x || display_size.y != timeline_display_size_.y) {
    int width = (int)math::max(display_size.x, 16.0f);
    int height = (int)math::max(display_size.y, 16.0f);
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
    redraw_ = true;
    Log::debug("Timeline framebuffer resized ({}x{})", (int)width, (int)height);
  }

  view_scale_ = timeline_get_view_scale();
  const double inv_view_scale = 1.0 / view_scale_;
  const double scroll_pos_x = timeline_get_scroll_pos_x();
  const GridProperties grid_props = get_grid_properties(grid_mode_);
  scroll_offset_x_ = (double)view_pos_.x - scroll_pos_x;

  timeline_handle_mouse_event();
  timeline_handle_track_event();
  font_push(FontType::Normal, 13.0f);

  if (redraw_) {
    ImTextureRef font_tex_ref = ImGui::GetIO().Fonts->TexRef;
    layer1_dl_->_ResetForNewFrame();
    layer2_dl_->_ResetForNewFrame();
    layer3_dl_->_ResetForNewFrame();
    layer1_dl_->PushTexture(font_tex_ref);
    layer2_dl_->PushTexture(font_tex_ref);
    layer3_dl_->PushTexture(font_tex_ref);
    layer1_dl_->PushClipRect(view_min_, view_max_);
    layer2_dl_->PushClipRect(view_min_, view_max_);
    layer3_dl_->PushClipRect(view_min_, view_max_);
    waveform_cmd.resize(0);

    const double beat_duration = Engine2::get_beat_duration();
    const double sample_scale = view_scale_ * beat_duration;
    const ImU32 track_line_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.85f).to_uint32();
    const ImU32 label_line = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.45f).to_uint32();

    const bool selecting_range = timeline_state_.type == TimelineState::Select || timeline_state_.select.is_selected;
    double selection_start_pos = timeline_state_.select.start_pos;
    double selection_end_pos = timeline_state_.select.end_pos;
    int32_t first_selected_track = timeline_state_.select.first_track_id;
    int32_t last_selected_track = timeline_state_.select.last_track_id;

    if (selecting_range) {
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

    float track_pos_y = first_visible_track_pos_y_;
    for (int32_t i = first_visible_track_; i < last_visible_track_; i++) {
      Track* track = Engine2::tracks[i];
      const float height = track->get_height();
      const float track_pos_abs_y = view_min_.y + track_pos_y;
      const bool mini_clip = height <= 30.0f;

      for (uint32_t j = 0; j < track->clips.size(); j++) {
        Clip* clip = track->clips[j];
        const double start_pos = clip->min_time * inv_view_scale;
        const double end_pos = clip->max_time * inv_view_scale;
        const float x0 = (float)(scroll_offset_x_ + math::round(start_pos) + 0.75);
        const float x1 = (float)(scroll_offset_x_ + math::round(end_pos));

        if (x0 >= view_max_.x)
          break;
        if (x1 < view_min_.x)
          continue;

        const float x0_clipped = math::max(x0, view_min_.x - 3.0f);
        const float x1_clipped = math::min(x1, view_max_.x + 3.0f);
        const float clip_label_max_y = track_pos_abs_y + font_size_ + 4.0f;

        const ImVec2 clip_label_min_bb(x0_clipped, track_pos_abs_y);
        const ImVec2 clip_label_max_bb(x1_clipped, clip_label_max_y);
        const ImVec2 clip_content_min(x0_clipped, clip_label_max_y);
        const ImVec2 clip_content_max(x1_clipped, track_pos_abs_y + height);

        const Color color(clip->color);
        const Color label_color = color.darken(0.10f);
        const Color content_color = color.brighten(1.3f);
        const ColorU32 bg_color = color.change_alpha(color.a * 0.80f).premult_alpha().to_uint32();
        const ColorU32 label_color_u32 = label_color.to_uint32();
        const ColorU32 content_color_u32 = content_color.to_uint32();

        layer1_dl_->AddRectFilled(clip_label_min_bb, clip_content_max, bg_color, 3.0f, ImDrawFlags_RoundCornersTop);

        switch (clip->type) {
          case ClipType::Audio: {
            AudioAsset* asset = clip->audio.asset;

            if (!asset || mini_clip) {
              break;
            }

            WaveformVisual* waveform = asset->waveform_visual;
            static constexpr double log_base4 = 1.0 / 1.3862943611198906;  // 1.0 / log(4.0)
            const double start_offset = clip->start_offset;
            const double scale_x = sample_scale * (double)waveform->sample_rate * clip->audio.speed;
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

            const double waveform_len = ((double)waveform->sample_count - start_offset) * inv_scale_x;
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
              // auto& waveform_cmd_list = !draw_in_layer2 ? waveform_cmd_list1 : waveform_cmd_list2;
              double waveform_start = start_offset * inv_scale_x;
              const double start_idx = std::round(math::max(-rel_min_x, 0.0) + waveform_start);
              const float min_bb_x = (float)math::round(min_pos_x);
              const float max_bb_x = (float)math::round(max_pos_x);
              const float pos_y = clip_content_min.y - offset_y;
              if (waveform->channels == 2) {
                const float height = std::floor((clip_content_max.y - clip_content_min.y) * 0.5f);
                waveform_cmd.push_back(
                    {
                      .waveform_vis = waveform,
                      .min_x = min_bb_x,
                      .min_y = pos_y,
                      .max_x = max_bb_x,
                      .max_y = pos_y + height,
                      .gain = clip->audio.gain,
                      .scale_x = (float)mip_scale,
                      .gap_size = gap_size,
                      .color = content_color_u32,
                      .mip_index = index,
                      .channel = 0,
                      .start_idx = (uint32_t)start_idx,
                      .draw_count = (uint32_t)draw_count + 2,
                    });
                waveform_cmd.push_back(
                    {
                      .waveform_vis = waveform,
                      .min_x = min_bb_x,
                      .min_y = pos_y + height,
                      .max_x = max_bb_x,
                      .max_y = pos_y + height * 2.0f,
                      .gain = clip->audio.gain,
                      .scale_x = (float)mip_scale,
                      .gap_size = gap_size,
                      .color = content_color_u32,
                      .mip_index = index,
                      .channel = 1,
                      .start_idx = (uint32_t)start_idx,
                      .draw_count = (uint32_t)draw_count + 2,
                    });
              } else {
                waveform_cmd.push_back(
                    {
                      .waveform_vis = waveform,
                      .min_x = min_bb_x,
                      .min_y = pos_y,
                      .max_x = max_bb_x,
                      .max_y = clip_content_max.y - offset_y,
                      .gain = clip->audio.gain,
                      .scale_x = (float)mip_scale,
                      .gap_size = gap_size,
                      .color = content_color_u32,
                      .mip_index = index,
                      .start_idx = (uint32_t)start_idx,
                      .draw_count = (uint32_t)draw_count + 2,
                    });
              }
              break;
            }
          }
          default: break;
        }

        if (clip->name.size() != 0) {
          timeline_draw_clip_label(layer1_dl_, clip->name, mini_clip, height, clip_label_min_bb, clip_label_max_bb);
        }
      }

      if (selecting_range && math::in_range(i, first_selected_track, last_selected_track)) {
        // static const ImU32 selection_range_fill = ImColor(28, 150, 237, 90);
        // static const ImU32 selection_range_border = ImColor(28, 150, 237, 255);
        static const ImU32 selection_range_fill = ImColor(0, 120, 215, 90);
        static const ImU32 selection_range_border = ImColor(28, 150, 237, 255);
        double x0 = math::round(selection_start_pos * inv_view_scale);
        double x1 = math::round(selection_end_pos * inv_view_scale);
        const ImVec2 min_bb((float)(scroll_offset_x_ + x0), track_pos_abs_y);
        const ImVec2 max_bb((float)(scroll_offset_x_ + x1), track_pos_abs_y + height);
        layer3_dl_->AddRectFilled(min_bb, max_bb, selection_range_fill);
        layer3_dl_->AddLine(min_bb, ImVec2(min_bb.x, max_bb.y), selection_range_border);
        layer3_dl_->AddLine(ImVec2(max_bb.x, min_bb.y), max_bb, selection_range_border);
      }

      if (timeline_state_.type == TimelineState::DragDropFiles) {
        if (!timeline_state_.drag_drop_files.item_dropped && timeline_state_.drag_drop_files.first_track_id == i) {
          BrowserFilePayload* payload_data = timeline_state_.drag_drop_files.payload_data;
          double length = 1.0;

          if (payload_data->type == BrowserItem::Sample) {
            length = samples_to_beat(payload_data->content_length, payload_data->sample_rate, beat_duration);
          }

          const double highlight_pos = timeline_state_.drag_drop_files.position;
          const double x0 = highlight_pos * inv_view_scale;
          const double x1 = (highlight_pos + length) * inv_view_scale;
          const ImVec2 min_bb((float)(scroll_offset_x_ + x0), track_pos_abs_y);
          const ImVec2 max_bb((float)(scroll_offset_x_ + x1), track_pos_abs_y + height);
          std::string_view filename = timeline_state_.drag_drop_files.payload_data->filename;
          layer3_dl_->AddRectFilled(min_bb, max_bb, highlight_color_);
          timeline_draw_clip_label(layer3_dl_, filename, mini_clip, height, min_bb, max_bb);
        }
      }

      track_pos_y += height;
      // Divisor line
      im_draw_hline(layer1_dl_, view_min_.y + track_pos_y + 0.5f, view_min_.x, view_max_.x, track_line_color);
      track_pos_y += 2.0f;
    }

    layer3_dl_->PopClipRect();
    layer2_dl_->PopClipRect();
    layer1_dl_->PopClipRect();
    layer3_dl_->PopTexture();
    layer2_dl_->PopTexture();
    layer1_dl_->PopTexture();

    ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();
    g_renderer->begin_render(timeline_fb_, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    layer_draw_data_.Clear();
    layer_draw_data_.DisplayPos = view_min_;
    layer_draw_data_.DisplaySize = display_size;
    layer_draw_data_.FramebufferScale.x = 1.0f;
    layer_draw_data_.FramebufferScale.y = 1.0f;
    layer_draw_data_.OwnerViewport = owner_viewport;
    layer_draw_data_.Textures = &ImGui::GetPlatformIO().Textures;
    layer_draw_data_.AddDrawList(layer1_dl_);
    g_renderer->render_imgui_draw_data(&layer_draw_data_);
    gfx_draw_waveform_batch(waveform_cmd, 0, 0, (int32_t)display_size.x, (int32_t)display_size.y);

    layer_draw_data_.Clear();
    layer_draw_data_.DisplayPos = view_min_;
    layer_draw_data_.DisplaySize = display_size;
    layer_draw_data_.FramebufferScale.x = 1.0f;
    layer_draw_data_.FramebufferScale.y = 1.0f;
    layer_draw_data_.OwnerViewport = owner_viewport;
    layer_draw_data_.Textures = &ImGui::GetPlatformIO().Textures;
    layer_draw_data_.AddDrawList(layer2_dl_);
    g_renderer->render_imgui_draw_data(&layer_draw_data_);

    layer_draw_data_.Clear();
    layer_draw_data_.DisplayPos = view_min_;
    layer_draw_data_.DisplaySize = display_size;
    layer_draw_data_.FramebufferScale.x = 1.0f;
    layer_draw_data_.FramebufferScale.y = 1.0f;
    layer_draw_data_.OwnerViewport = owner_viewport;
    layer_draw_data_.Textures = &ImGui::GetPlatformIO().Textures;
    layer_draw_data_.AddDrawList(layer3_dl_);
    g_renderer->render_imgui_draw_data(&layer_draw_data_);

    g_renderer->end_render();
  }

  font_pop();

  ImTextureID fb_tex_id = (ImTextureID)timeline_fb_;
  dl->AddImage(fb_tex_id, view_min_, view_min_ + display_size);

  if (Engine2::is_playing()) {
    const double playhead_offset = Engine2::playhead * inv_view_scale;
    const float playhead_pos = (float)math::round(view_pos_.x - scroll_pos_x + playhead_offset);
    im_draw_vline(dl, playhead_pos, view_min_.y, view_max_.y, playhead_color_);
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
    view_state_.zoom(mouse_wheel_ * zoom_sensitivity_, mouse_pos_.x - view_pos_.x, max_length_, view_scale_);
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
    const ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
    timeline_hscroll(drag_delta.x);
    scroll_delta_y_ = drag_delta.y;
    if (drag_delta.x != 0.0f || drag_delta.y != 0.0f)
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

  // Handle automatic scrolling
  if (dragging_file || timeline_state_.type == TimelineState::Select) {
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
    timeline_state_.type = TimelineState::DragDropFiles;
    timeline_state_.drag_drop_files = {
      .payload_data = drop_payload_data,
      .first_track_id = -1,
      .item_dropped = item_dropped,
    };
  }

  if (timeline_state_.select.is_selected && left_mouse_clicked_) {
    timeline_state_.clear_selection();
    redraw_ = true;
  }

  if (timeline_hovered_ && can_select_) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
  }

  if (timeline_state_.type == TimelineState::DragDropFiles && !dragging_file) {
    timeline_state_.end_action();
  }
}

void timeline_handle_track_event() {
  const double hovered_position = timeline_get_hovered_position();
  const double inv_view_scale = 1.0 / view_scale_;

  float track_pos_y = -vscroll_;
  float first_visible_pos_y = INFINITY;
  float relative_mouse_pos_y = mouse_pos_.y - view_min_.y;

  int32_t track_count = (int32_t)Engine2::tracks.size();
  int32_t track_idx = 0;
  std::optional<int32_t> hovered_track_id;
  bool require_drag = !any_of(timeline_state_.type, TimelineState::DragDropFiles, TimelineState::Select);

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
    bool hovered = rect_hovered && timeline_focused_;

    if (first_visible_pos_y == INFINITY) {
      first_visible_pos_y = track_pos_y;
      first_visible_track_ = track_idx;
    }

    if (hovered) {
      hovered_track_id = track_idx;

      if (can_select_ && left_mouse_clicked_) {
        timeline_state_.type = TimelineState::Select;
        timeline_state_.select = {
          .start_pos = hovered_position,
          .first_track_id = track_idx,
          .last_track_id = track_idx,
        };
      }

      uint32_t count = (uint32_t)track->clips.size();
      for (uint32_t i = 0; i < count; i++) {
        Clip* clip = track->clips[i];
        const double start_pos = clip->min_time * inv_view_scale;
        const double end_pos = clip->max_time * inv_view_scale;
        const float x0_min = (float)(scroll_offset_x_ + math::round(start_pos));
        const float x1_max = (float)(scroll_offset_x_ + math::round(end_pos));

        if (x0_min >= view_max_.x)
          break;
        if (x1_max < view_min_.x)
          continue;

        constexpr float handle_size = 4.0f;
        const float x0_max = x0_min + handle_size;
        const float x1_min = x1_max - handle_size;

        if (math::in_range(mouse_pos_.x, x1_min, x1_max)) {
          // Right handle
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else if (math::in_range(mouse_pos_.x, x0_min, x0_max)) {
          // Left handle
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else if (math::in_range(mouse_pos_.x, x0_min, x1_max)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
      }

      if (can_select_) {
      }
    }

    /*if (timeline_state_.type == TimelineState::Select) {
      if (left_mouse_down_ && hovered) {
        timeline_state_.select.end_pos = hovered_position;
        timeline_state_.select.last_track_id = track_idx;
      }
    }*/

    if (timeline_state_.type == TimelineState::DragDropFiles) {
      if (rect_hovered) {
        timeline_state_.drag_drop_files.first_track_id = track_idx;
        timeline_state_.drag_drop_files.position = hovered_position;
        redraw_ = true;
      }
    }

    track_pos_y = next_pos_y;
  }

  if (timeline_state_.type == TimelineState::DragDropFiles) {
    if (timeline_state_.drag_drop_files.item_dropped) {
      timeline_add_clip_from_file();
    }
  } else if (timeline_state_.type == TimelineState::Select) {
    if (left_mouse_down_ && hovered_track_id) {
      timeline_state_.select.last_track_id = hovered_track_id.value();
    }

    if (left_mouse_down_) {
      timeline_state_.select.end_pos = hovered_position;
      redraw_ = true;
    }

    if (!left_mouse_down_) {
      auto& [is_selected, start_pos, end_pos, first_track_id, last_track_id] = timeline_state_.select;
      is_selected = true;
      if (start_pos > end_pos) {
        std::swap(start_pos, end_pos);
      }
      if (first_track_id > last_track_id) {
        std::swap(first_track_id, last_track_id);
      }
      timeline_state_.end_action();
      timeline_query_selected_range();
    }
  }

  first_visible_track_pos_y_ = first_visible_pos_y;
  last_visible_track_ = track_idx;
}

void timeline_query_selected_range() {
  int32_t first = timeline_state_.select.first_track_id;
  int32_t last = timeline_state_.select.last_track_id;
  timeline_state_.selected_track_clips.reserve(last - first + 1);
  Log::debug("Selected tracks:");
  for (int32_t i = first; i <= last; i++) {
    Track* track = Engine2::tracks[i];
    auto query_result = track->query_clip_by_range2(timeline_state_.select.start_pos, timeline_state_.select.end_pos);
    if (query_result) {
      timeline_state_.selected_track_clips.push_back(query_result);
      Log::debug(
          "Track {}: {} {} {} {}",
          i,
          query_result.first,
          query_result.last,
          query_result.first_offset,
          query_result.last_offset);
    } else {
      timeline_state_.selected_track_clips.emplace_back();  // Insert empty result
    }
  }
}

void timeline_add_track() {
  float hue_index = (float)track_color_spin_ / 15.0f;
  // float sat_amount = 0.15f * std::fmod(1.0f - 2.0f * hue_index, 1.0f) + 0.85f;
  CmdAddTrack* cmd = new CmdAddTrack();
  cmd->name = "New track";
  cmd->color = Color::from_hsluv(hue_index, 0.86f, 0.5943f);
  // cmd->color = Color::from_hsv(hue_index, 0.6321f, 0.90f);
  // cmd->color = Color::from_hsv((float)track_color_spin_ / 15.0f, 0.6172f, 0.80f);

  CommandManager2::execute_command("Add track", cmd);
  track_color_spin_ = (track_color_spin_ + 1) % 15;
  redraw_ = true;
}

void timeline_add_clip_from_file() {
  CmdAddClipFromFile* cmd = new CmdAddClipFromFile();
  cmd->track_id = timeline_state_.drag_drop_files.first_track_id;
  cmd->position = timeline_state_.drag_drop_files.position;
  cmd->file_path = timeline_state_.drag_drop_files.payload_data->path;
  CommandManager2::execute_command("Add clip", cmd);
}

}  // namespace wb