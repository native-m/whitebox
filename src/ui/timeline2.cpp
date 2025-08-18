#include "timeline2.h"

#include <imgui.h>

#include "controls.h"
#include "engine/engine2.h"
#include "engine/track.h"
#include "gfx/renderer.h"
#include "layout.h"
#include "timeline_controls.h"
#include "context_menu.h"

namespace wb {

static double view_start_pos = 0.0;
static double view_end_pos = 1.0;
static double view_scale;
static double max_length;
static float track_panel_width = 150.0f;
static float track_panel_min_width = 100.0f;
static bool redraw = true;
static bool force_redraw;

static ImU32 text_color;
static ImU32 text_transparent_color;
static uint32_t track_color_spin;

static GPUTexture* timeline_fb;
static ImDrawList* main_dl;
static ImDrawList* layer1_dl;
static ImDrawList* layer2_dl;
static ImDrawList* layer3_dl;
static ImDrawData layer_draw_data;
static ImFont* font;
static float font_size;

static Track* context_menu_track{};
static uint32_t context_menu_track_id{};
static Clip* context_menu_clip{};
static Color tmp_color;
static std::string tmp_name;

bool g_timeline2_window_open = true;

static void timeline_render_navbar();
static void timeline_render_track_panel();
static void timeline_render_track_lanes();
static void timeline_add_track();

void timeline_init() {
  Engine2::add_bpm_update_listener(nullptr, [](void* userdata, double beat_duration, double bpm) { force_redraw = true; });
  layer1_dl = new ImDrawList(ImGui::GetDrawListSharedData());
  layer2_dl = new ImDrawList(ImGui::GetDrawListSharedData());
  layer3_dl = new ImDrawList(ImGui::GetDrawListSharedData());
}

void timeline_shutdown() {
  delete layer1_dl;
  delete layer2_dl;
  delete layer3_dl;
  if (timeline_fb)
    g_renderer->destroy_texture(timeline_fb);
}

void timeline_redraw_window() {
  force_redraw = true;
}

void render_timeline() {
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  if (!controls::begin_window("Timeline 2", &g_timeline2_window_open)) {
    ImGui::PopStyleVar();
    controls::end_window();
    return;
  }

  redraw = force_redraw;
  if (force_redraw)
    force_redraw = false;

  text_color = ImGui::GetColorU32(ImGuiCol_Text);
  text_transparent_color = Color(ImGui::GetColorU32(ImGuiCol_Text)).change_alpha(0.7f).to_uint32();

  timeline_render_navbar();

  ImVec2 timeline_layout_size = ImGui::GetContentRegionAvail();
  layout::begin_columns("tl_cols", 2, timeline_layout_size);
  layout::next_column(track_panel_width, 100.0f);
  timeline_render_track_panel();
  layout::next_column(0.0f);
  timeline_render_track_lanes();
  layout::end_columns();

  ImGui::PopStyleVar();
  controls::end_window();
}

void timeline_render_navbar() {
  static float separator_size = 2.0f;
  ImVec2 window_size = ImGui::GetContentRegionAvail();
  float navbar_size = window_size.x - track_panel_width;

  ImGui::SetCursorPosX(math::max(track_panel_width, track_panel_min_width) + separator_size);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  ImGui::BeginGroup();

  if (controls::timeline_scrollbar("tl_hscroll", navbar_size, 0.1, 80.0, &view_start_pos, &view_end_pos)) {
    // TODO(native-m): redraw the timeline when scrolling
  }

  double time_pos = Engine2::playhead;
  if (auto ret = controls::timeline_ruler("tl_ruler", navbar_size, 80.0, &view_start_pos, &view_end_pos, &time_pos);
      ret != controls::TimelineRulerResult::None) {
    switch (ret) {
      case controls::TimelineRulerResult::Zoom: redraw = true; break;
      case controls::TimelineRulerResult::TimePositionChanged: Engine2::set_playhead_position(time_pos); break;
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
  const auto& style = ImGui::GetStyle();
  track_panel_width = layout::get_current_column_width();

  //static float volume = 0.0f;
  //static float pan = 0.0f;
  bool open_track_context_menu = false;

  for (uint32_t i = 0; i < num_tracks; i++) {
    Track* track = Engine2::tracks[i];
    float height = track->height;
    ImVec2 color_size(track_color_width, height);
    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImVec2 end_pos = start_pos + ImVec2(track_panel_width, height);
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

    float track_controls_width = track_panel_width - track_color_width - vu_meter_width;
    ImVec2 track_controls_size(track_controls_width, height);
    ImVec2 track_controls_min_bb = ImGui::GetCursorScreenPos() + padding;
    ImVec2 track_controls_max_bb = track_controls_min_bb + track_controls_size;

    if (ImGui::BeginChild("##track_controls", track_controls_size, 0, track_control_window_flags)) {
      float volume = track->ui_parameter_state.volume_db;
      bool mute = track->ui_parameter_state.mute;

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);

      if (controls::collapse_button2("##track_collapse", &track->shown)) {
        redraw = true;
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

          ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding
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
        context_menu_track = track;
        context_menu_track_id = i;
        tmp_color = track->color;
        tmp_name = track->name;
        open_track_context_menu = true;
      }

      ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
    }

    ImGui::EndChild();

    ImGui::SameLine();
    controls::level_meter(
        "##timeline_vu_meter", ImVec2(10.0f, height), 2, track->level_meter, track->level_meter_color, false);

    if (controls::hsplitter(i, &height, 60.0f, 20.f, 600.f, track_panel_width)) {
      if (track->shown)
        track->height = height;
      redraw = true;
    }

    ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding
    ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
    ImGui::PopID();
  }

  if (open_track_context_menu) {
    ImGui::OpenPopup("track_context_menu");
  }

  if (ImGui::BeginPopup("track_context_menu")) {
    if (track_context_menu(context_menu_track, context_menu_track_id, &tmp_name, &tmp_color)) {
      redraw = true;
    }
    ImGui::EndPopup();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  ImGui::BeginChild("track_add", ImVec2(track_panel_width, 60.0f), 0, track_control_window_flags);
  if (ImGui::Button("+ Track", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    timeline_add_track();
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void timeline_render_track_lanes() {
  ImGui::Button("Test");
}

void timeline_add_track() {
  // TODO(native-m): Replace with command
  Color color = Color::from_hsv((float)track_color_spin / 15.0f, 0.6172f, 0.80f);
  Engine2::create_track("New track", color, 60.0f);
  track_color_spin = (track_color_spin + 1) % 15;
  redraw = true;
}

}  // namespace wb