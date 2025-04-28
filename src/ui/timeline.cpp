#include "timeline.h"

#include "IconsMaterialSymbols.h"
#include "browser.h"
#include "clip_editor.h"
#include "command_manager.h"
#include "context_menu.h"
#include "controls.h"
#include "core/color.h"
#include "core/debug.h"
#include "dialogs.h"
#include "engine/engine.h"
#include "font.h"
#include "grid.h"
#include "plugins.h"
#include "window.h"

#define DEBUG_MIDI_CLIPS 0

#ifdef NDEBUG
#undef DEBUG_MIDI_CLIPS
#define DEBUG_MIDI_CLIPS 0
#endif

namespace wb {
TimelineWindow g_timeline;

void TimelineWindow::init() {
  g_engine.add_on_bpm_change_listener([this](double bpm, double beat_duration) { force_redraw = true; });
  g_cmd_manager.add_on_history_update_listener([this] { force_redraw = true; });
  layer1_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
  layer2_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
  layer3_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
}

void TimelineWindow::shutdown() {
  delete layer1_draw_list;
  delete layer2_draw_list;
  delete layer3_draw_list;
  if (timeline_fb)
    g_renderer->destroy_texture(timeline_fb);
}

void TimelineWindow::reset() {
  selected_track_regions.clear();
  selecting_range = false;
  range_selected = false;
  force_redraw = true;
  finish_edit();
  color_spin = 0;
}

void TimelineWindow::render() {
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  if (!controls::begin_window("Timeline", &g_timeline_window_open)) {
    ImGui::PopStyleVar();
    controls::end_window();
    return;
  }

  ImGui::PopStyleVar();

  redraw = force_redraw;
  if (force_redraw)
    force_redraw = false;

  playhead = g_engine.playhead_ui.load(std::memory_order_relaxed);
  beat_duration = g_engine.beat_duration.load(std::memory_order_relaxed);
  ppq = g_engine.ppq;
  inv_ppq = 1.0 / ppq;

  text_color = ImGui::GetColorU32(ImGuiCol_Text);
  text_transparent_color = Color(ImGui::GetColorU32(ImGuiCol_Text)).change_alpha(0.7f).to_uint32();
  splitter_color = ImGui::GetColorU32(ImGuiCol_Separator);
  splitter_hover_color = ImGui::GetColorU32(ImGuiCol_ResizeGripHovered);
  splitter_active_color = ImGui::GetColorU32(ImGuiCol_ResizeGripActive);

  font = ImGui::GetFont();
  font_size = ImGui::GetFontSize();
  mouse_pos = ImGui::GetMousePos();
  mouse_wheel = GImGui->IO.MouseWheel;
  mouse_wheel_h = GImGui->IO.MouseWheelH;

  timeline_window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
  has_deleted_clips = g_engine.has_deleted_clips.load(std::memory_order_relaxed);
  left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  holding_shift = ImGui::IsKeyDown(ImGuiKey_ModShift);
  holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
  holding_alt = ImGui::IsKeyDown(ImGuiKey_ModAlt);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  render_horizontal_scrollbar();
  double new_playhead_pos = 0.0;
  if (render_time_ruler(&new_playhead_pos)) {
    g_engine.set_playhead_position(new_playhead_pos);
  }
  ImGui::PopStyleVar();

  if (ImGui::BeginChild("timeline_content")) {
    main_draw_list = ImGui::GetWindowDrawList();
    content_min = ImGui::GetWindowContentRegionMin();
    content_max = ImGui::GetWindowContentRegionMax();
    content_size = content_max - content_min;
    vscroll = ImGui::GetScrollY();

    ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
    if (scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
      ImGui::SetScrollY(vscroll - scroll_delta_y);
      scroll_delta_y = 0.0f;
      redraw = true;
    }

    if ((last_vscroll - vscroll) != 0.0f)
      redraw = true;

    render_splitter();
    render_track_controls();
    render_track_lanes();
    last_vscroll = vscroll;
  }
  ImGui::EndChild();

  controls::end_window();
}

void TimelineWindow::render_splitter() {
  ImVec2 backup_cursor_pos = ImGui::GetCursorScreenPos();
  const float splitter_pos_x = backup_cursor_pos.x + vsplitter_size;
  ImVec2 splitter_pos(splitter_pos_x - 2.0f, backup_cursor_pos.y + vscroll);
  ImGui::SetCursorScreenPos(splitter_pos);

  ImGui::InvisibleButton("##timeline_splitter", ImVec2(4.0f, content_size.y));
  bool is_splitter_hovered = ImGui::IsItemHovered();
  bool is_splitter_active = ImGui::IsItemActive();
  ImU32 color = splitter_color;

  // Change the color
  if (is_splitter_active) {
    color = splitter_active_color;
  } else if (is_splitter_hovered) {
    color = splitter_hover_color;
  }

  if (is_splitter_hovered || is_splitter_active) {
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      vsplitter_size = vsplitter_default_size;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }

  // Adjust splitter size
  if (is_splitter_active) {
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    vsplitter_size += drag_delta.x;
    redraw = true;
  } else {
    vsplitter_size = math::max(vsplitter_size, vsplitter_min_size);
  }

  const float separator_x = splitter_pos_x + 0.5f;
  main_draw_list->AddLine(
      ImVec2(separator_x, splitter_pos.y), ImVec2(separator_x, splitter_pos.y + content_size.y), color, 2.0f);

  // Restore the previous cursor pos
  ImGui::SetCursorScreenPos(backup_cursor_pos);

  timeline_view_pos.x = splitter_pos_x + 2.0f;
  timeline_view_pos.y = backup_cursor_pos.y;
}

void TimelineWindow::render_track_controls() {
  constexpr ImGuiWindowFlags track_control_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                          ImGuiWindowFlags_NoBackground |
                                                          ImGuiWindowFlags_AlwaysUseWindowPadding;

  static constexpr ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);
  static constexpr float track_color_width = 8.0f;
  static constexpr float vu_meter_space = 11.0f;
  const bool is_recording = g_engine.is_recording();
  const auto& style = ImGui::GetStyle();
  auto& tracks = g_engine.tracks;
  uint32_t num_tracks = (uint32_t)g_engine.tracks.size();
  bool open_track_context_menu = false;
  bool move_track = false;
  uint32_t move_track_src = 0;
  uint32_t move_track_dst = 0;

  for (uint32_t i = 0; i < num_tracks; i++) {
    Track* track = tracks[i];
    float height = track->get_height();
    const char* begin_name_str = track->name.c_str();
    const char* end_name_str = begin_name_str + track->name.size();
    const ImVec2 tmp_item_spacing = style.ItemSpacing;
    const ImVec2 track_color_min = ImGui::GetCursorScreenPos();
    const ImVec2 track_color_max = ImVec2(track_color_min.x + track_color_width, track_color_min.y + height);

    if (ImGui::IsRectVisible(track_color_min, track_color_max)) {
      main_draw_list->AddRectFilled(track_color_min, track_color_max, track->color.to_uint32());
    }

    ImGui::PushID(i);
    ImGui::Indent(track_color_width);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));

    const ImVec2 size = ImVec2(vsplitter_size - track_color_width - vu_meter_space, height);
    const ImVec2 pos_start = ImGui::GetCursorScreenPos();
    const ImVec2 pos_end = pos_start + size;

    if (ImGui::BeginChild("##track_control", size, 0, track_control_window_flags)) {
      ImGuiSliderFlags slider_flags = ImGuiSliderFlags_Vertical;
      float volume = track->ui_parameter_state.volume_db;
      bool mute = track->ui_parameter_state.mute;

      ImGui::PopStyleVar();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
      if (controls::collapse_button("##track_collapse", &track->shown)) {
        redraw = true;
      }
      ImGui::PopStyleVar();

      ImGui::SameLine(0.0f, 5.0f);
      if (track->name.size() > 0) {
        ImGui::TextUnformatted(begin_name_str, end_name_str);
      } else {
        ImGui::BeginDisabled();
        ImGui::TextUnformatted("(unnamed)");
        ImGui::EndDisabled();
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
              g_engine.solo_track(i);

            ImGui::SameLine(0.0f, 2.0f);
            ImGui::BeginDisabled(is_recording);
            if (controls::small_toggle_button("R", &track->input_attr.armed, muted_color))
              g_engine.arm_track_recording(i, !track->input_attr.armed);
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
            g_engine.solo_track(i);

          ImGui::SameLine(0.0f, 2.0f);
          ImGui::BeginDisabled(is_recording);
          if (controls::toggle_button("R", &track->input_attr.armed, muted_color))
            g_engine.arm_track_recording(i, !track->input_attr.armed);
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("track_input_context_menu");
          ImGui::EndDisabled();

          ImGui::SameLine(0.0f, 2.0f);
          ImVec2 pos = ImGui::GetCursorPos();
          ImGui::SetNextItemWidth(-FLT_MIN);
          if (controls::param_drag_db("##Vol.", &volume))
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

          ImGui::PopStyleVar();
        }

        if (controls::small_toggle_button("M", &mute, muted_color))
          track->set_mute(!mute);
        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::SmallButton("S"))
          g_engine.solo_track(i);
        ImGui::SameLine(0.0f, 2.0f);

        ImGui::BeginDisabled(is_recording);
        if (controls::small_toggle_button("R", &track->input_attr.armed, muted_color))
          g_engine.arm_track_recording(i, !track->input_attr.armed);
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

      ImGui::PopStyleVar();
    } else {
      ImGui::PopStyleVar();
    }

    ImGui::EndChild();

    if (ImGui::BeginDragDropTarget()) {
      static constexpr auto drag_drop_flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

      // Custom highlighter
      if (auto payload = ImGui::AcceptDragDropPayload("WB_MOVE_TRACK", ImGuiDragDropFlags_AcceptPeekOnly)) {
        main_draw_list->AddLine(
            pos_start, ImVec2(pos_end.x, pos_start.y), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
      } else if (ImGui::GetDragDropPayload()) {
        main_draw_list->AddRect(pos_start, pos_end, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 0, 2.0f);
      }

      if (auto payload = ImGui::AcceptDragDropPayload("WB_PLUGINDROP", drag_drop_flags)) {
        PluginItem* item;
        std::memcpy(&item, payload->Data, payload->DataSize);
        add_plugin(track, item->uid);
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

    ImGui::PopID();
    ImGui::Unindent(track_color_width);

    const float total_width = pos_end.x - pos_start.x + track_color_width + vu_meter_space;
    if (controls::hsplitter(i, &height, 60.0f, 20.f, 600.f, total_width)) {
      track->height = height;
      redraw = true;
    }

    ImGui::PopStyleVar();
  }

  if (move_track) {
    TrackMoveCmd* cmd = new TrackMoveCmd();
    cmd->src_slot = move_track_src;
    cmd->dst_slot = move_track_dst;
    g_cmd_manager.execute("Move track", cmd);
    redraw = true;
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
  ImGui::BeginChild("track_add", ImVec2(vsplitter_size, 60.0f), 0, track_control_window_flags);
  if (ImGui::Button("+ Track", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
    add_track();
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void TimelineWindow::render_clip_context_menu() {
  bool open_rename_popup = false;
  bool open_change_color_popup = false;

  if (ImGui::BeginPopup("clip_context_menu")) {
    if (ImGui::MenuItem("Rename"))
      open_rename_popup = true;

    if (ImGui::MenuItem("Change color"))
      open_change_color_popup = true;

    ImGui::Separator();

    if (!context_menu_clip->is_active()) {
      if (ImGui::MenuItem("Activate Clip")) {
        context_menu_clip->set_active(true);
        force_redraw = true;
      }
    } else {
      if (ImGui::MenuItem("Deactivate Clip")) {
        context_menu_clip->set_active(false);
        force_redraw = true;
      }
    }

    if (ImGui::MenuItem("Delete")) {
      Track* track = g_engine.tracks[context_menu_track_id];
      if (track == g_clip_editor.current_track && context_menu_clip->id == g_clip_editor.current_clip->id) {
        g_clip_editor.unset_clip();
      }

      double beat_duration = g_engine.get_beat_duration();
      ClipDeleteCmd* cmd = new ClipDeleteCmd();
      cmd->track_id = context_menu_track_id;
      cmd->clip_id = context_menu_clip->id;
      g_cmd_manager.execute("Delete Clip", cmd);
      recalculate_song_length();
      force_redraw = true;
    }

    if (ImGui::MenuItem("Duplicate")) {
      // TODO
      force_redraw = true;
    }

    ImGui::EndPopup();
  }

  if (open_rename_popup) {
    ImGui::OpenPopup("rename_clip");
  }

  if (open_change_color_popup) {
    ImGui::OpenPopup("change_clip_color");
  }

  if (context_menu_clip) {
    bool cleanup = false;

    if (auto ret = rename_dialog("rename_clip", tmp_name, &context_menu_clip->name)) {
      switch (ret) {
        case ConfirmDialog::ValueChanged: force_redraw = true; break;
        case ConfirmDialog::Ok: {
          ClipRenameCmd* cmd = new ClipRenameCmd();
          cmd->track_id = context_menu_track_id;
          cmd->clip_id = context_menu_clip->id;
          cmd->old_name = tmp_name;
          cmd->new_name = context_menu_clip->name;
          g_cmd_manager.execute("Rename clip", cmd);
          force_redraw = true;
          cleanup = true;
          break;
        }
        case ConfirmDialog::Cancel:
          force_redraw = true;
          cleanup = true;
          break;
        case ConfirmDialog::None: break;
        default: break;
      }
    }

    if (auto ret = color_picker_dialog("change_clip_color", tmp_color, &context_menu_clip->color)) {
      switch (ret) {
        case ConfirmDialog::ValueChanged: force_redraw = true; break;
        case ConfirmDialog::Ok: {
          ClipChangeColorCmd* cmd = new ClipChangeColorCmd();
          cmd->track_id = context_menu_track_id;
          cmd->clip_id = context_menu_clip->id;
          cmd->old_color = tmp_color;
          cmd->new_color = context_menu_clip->color;
          g_cmd_manager.execute("Change clip color", cmd);
          force_redraw = true;
          cleanup = true;
          break;
        }
        case ConfirmDialog::Cancel:
          force_redraw = true;
          cleanup = true;
          break;
        case ConfirmDialog::None: break;
        default: break;
      }
    }

    if (cleanup) {
      context_menu_clip = nullptr;
      context_menu_track = nullptr;
    }
  }
}

void TimelineWindow::render_track_lanes() {
  ImGui::SetCursorScreenPos(timeline_view_pos);
  const float offset_y = vscroll + timeline_view_pos.y;
  const auto timeline_area = ImGui::GetContentRegionAvail();
  const bool escape_key_pressed = timeline_window_focused && ImGui::IsKeyPressed(ImGuiKey_Escape);
  timeline_width = timeline_area.x;

  const ImVec2 view_min(timeline_view_pos.x, offset_y);
  const ImVec2 view_max(timeline_view_pos.x + timeline_width, offset_y + content_size.y);
  ImGui::PushClipRect(view_min, view_max, true);

  static constexpr uint32_t timeline_mouse_btn_flags =
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle;
  ImGui::InvisibleButton("##timeline", ImVec2(timeline_area.x, math::max(timeline_area.y, content_size.y + vscroll)));
  const bool timeline_clicked = ImGui::IsItemClicked();
  const bool timeline_hovered = ImGui::IsItemHovered();
  const bool mouse_move = false;
  double view_scale = calc_view_scale();
  double inv_view_scale = 1.0 / view_scale;
  timeline_bounds_min_x = view_min.x;
  timeline_bounds_min_y = view_min.y;
  timeline_bounds_max_x = view_max.x;

  // Resize timeline framebuffer
  if (timeline_width != old_timeline_size.x || content_size.y != old_timeline_size.y) {
    int width = (int)math::max(timeline_width, 16.0f);
    int height = (int)math::max(content_size.y, 16.0f);
    if (timeline_fb)
      g_renderer->destroy_texture(timeline_fb);
    timeline_fb = g_renderer->create_texture(
        GPUTextureUsage::Sampled | GPUTextureUsage::RenderTarget,
        GPUFormat::UnormB8G8R8A8,
        width,
        height,
        true,
        0,
        0,
        nullptr);
    Log::debug("Timeline framebuffer resized ({}x{})", (int)width, (int)height);
    old_timeline_size.x = timeline_width;
    old_timeline_size.y = content_size.y;
    redraw = redraw || true;
  }

  // Zoom
  if (timeline_hovered && holding_ctrl && mouse_wheel != 0.0f) {
    zoom(mouse_pos.x, timeline_view_pos.x, view_scale, mouse_wheel * zoom_rate);
    view_scale = calc_view_scale();
    inv_view_scale = 1.0 / view_scale;
  }

  // Do horizontal scroll
  if (timeline_hovered && mouse_wheel_h != 0.0f) {
    double scroll_speed = 64.0;
    scroll_horizontal(mouse_wheel_h, song_length, -view_scale * scroll_speed);
  }

  // Acquire scroll
  if (middle_mouse_clicked && middle_mouse_down && timeline_hovered) {
    scrolling = true;
  }

  // Do scroll
  if (scrolling) {
    const ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
    scroll_horizontal(drag_delta.x, song_length, -view_scale);
    scroll_delta_y = drag_delta.y;
    if (scroll_delta_y != 0.0f)
      redraw = true;
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
  }

  // Release scroll
  if (!middle_mouse_down) {
    scrolling = false;
    scroll_delta_y = 0.0f;
  }

  BrowserFilePayload* drop_payload_data{};
  bool dragging_file = false;
  bool item_dropped = false;
  // Handle file drag & drop
  if (ImGui::BeginDragDropTarget()) {
    auto payload = ImGui::GetDragDropPayload();
    if (payload->IsDataType("WB_FILEDROP")) {
      item_dropped = ImGui::AcceptDragDropPayload("WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
      std::memcpy(&drop_payload_data, payload->Data, payload->DataSize);
      dragging_file = true;
    }
    ImGui::EndDragDropTarget();
  }

  bool dragging = false;
  if (edit_command != TimelineCommand::None || dragging_file || selecting_range) {
    if (edit_command != TimelineCommand::ClipAdjustGain) {
      static constexpr float speed = 0.1f;
      static constexpr float drag_offset_x = 20.0f;
      static constexpr float drag_offset_y = 40.0f;
      float min_offset_x;
      float max_offset_x;
      float min_offset_y;
      float max_offset_y;

      if (!dragging_file) {
        min_offset_x = view_min.x;
        max_offset_x = view_max.x;
        min_offset_y = view_min.y;
        max_offset_y = view_max.y;
      } else {
        min_offset_x = view_min.x + drag_offset_x;
        max_offset_x = view_max.x - drag_offset_x;
        min_offset_y = view_min.y + drag_offset_y;
        max_offset_y = view_max.y - drag_offset_y;
      }

      // Scroll automatically when dragging stuff
      if (mouse_pos.x < min_offset_x) {
        float distance = min_offset_x - mouse_pos.x;
        scroll_horizontal(distance * speed, song_length, -view_scale);
      }
      if (mouse_pos.x > max_offset_x) {
        float distance = max_offset_x - mouse_pos.x;
        scroll_horizontal(distance * speed, song_length, -view_scale);
      }
      if (mouse_pos.y < min_offset_y) {
        float distance = min_offset_y - mouse_pos.y;
        scroll_delta_y = distance * speed;
      }
      if (mouse_pos.y > max_offset_y) {
        float distance = max_offset_y - mouse_pos.y;
        scroll_delta_y = distance * speed;
      }
    }

    // Find which track is currently hovered
    if (any_of(edit_command, TimelineCommand::ClipMove, TimelineCommand::ClipDuplicate)) {
      float track_pos_y = 0.0f;
      float mouse_pos_at_timeline_y = mouse_pos.y - timeline_view_pos.y;
      for (uint32_t i = 0; i < g_engine.tracks.size(); i++) {
        Track* track = g_engine.tracks[i];
        const float height = track->get_height();
        const float next_pos_y = track_pos_y + height + track_separator_height;
        if (mouse_pos_at_timeline_y >= track_pos_y && mouse_pos_at_timeline_y < next_pos_y) {
          hovered_track = track;
          hovered_track_id = i;
          hovered_track_y = track_pos_y + timeline_view_pos.y;
          hovered_track_height = height;
          break;
        }
        track_pos_y = next_pos_y;
      }
    }

    dragging = true;
    redraw = true;
  }

  const double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
  const double sample_scale = view_scale * beat_duration;
  const ImU32 gridline_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.85f).to_uint32();

  // Map mouse position to time position
  const double mouse_at_time_pos = ((double)(mouse_pos.x - timeline_view_pos.x) * view_scale + min_hscroll * song_length);
  const double mouse_at_gridline = std::round(mouse_at_time_pos * (double)grid_scale) / (double)grid_scale;

  timeline_scroll_offset_x = (double)timeline_view_pos.x - scroll_pos_x;
  timeline_scroll_offset_x_f32 = (float)timeline_scroll_offset_x;
  clip_scale = inv_view_scale;

  if (selecting_range) {
    selection_end_pos = math::max(mouse_at_gridline, 0.0);
    redraw = true;
  }

  // Pressing escape key cancel the selection
  if (selecting_range && escape_key_pressed) {
    selected_track_regions.clear();
    selecting_range = false;
    redraw = true;
  }

  // Release selection
  if (selecting_range && !left_mouse_down) {
    selecting_range = false;
    selection_end_pos = math::max(mouse_at_gridline, 0.0);
    range_selected = selection_end_pos != selection_start_pos;
    if (first_selected_track > last_selected_track) {
      std::swap(first_selected_track, last_selected_track);
    }
    if (selection_start_pos > selection_end_pos) {
      std::swap(selection_start_pos, selection_end_pos);
    }
    selected_track_regions.clear();
    query_selected_range();
  }

  if (holding_ctrl && timeline_hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
  }

  if (ImGui::IsItemFocused()) {
    if (range_selected) {
      if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        // Unset clip from the clip editor
        if (g_clip_editor.contains_clip()) {
          for (uint32_t track_idx = first_selected_track; const auto& region : selected_track_regions) {
            Track* track = g_engine.tracks[track_idx];
            uint32_t clip_id = g_clip_editor.current_clip->id;
            if (track == g_clip_editor.current_track && region.is_clip_selected(clip_id) != ClipSelectStatus::NotSelected) {
              g_clip_editor.unset_clip();
            }
            track_idx++;
          }
        }
        ClipDeleteCmd2* cmd = new ClipDeleteCmd2();
        cmd->selected_track_regions = selected_track_regions;
        cmd->first_track = first_selected_track;
        cmd->min_pos = selection_start_pos;
        cmd->max_pos = selection_end_pos;
        g_cmd_manager.execute("Delete Selected Region", cmd);
        recalculate_song_length();
        redraw = true;
      }

      if (holding_ctrl && holding_shift && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        CreateMidiClipCmd* cmd = new CreateMidiClipCmd();
        cmd->selected_track_regions = selected_track_regions;
        cmd->first_track = first_selected_track;
        cmd->min_pos = selection_start_pos;
        cmd->max_pos = selection_end_pos;
        g_cmd_manager.execute("Create MIDI clip", cmd);
        recalculate_song_length();
        redraw = true;
      }
    }
  }

  if (redraw) {
    ImTextureID font_tex_id = ImGui::GetIO().Fonts->TexID;
    clip_draw_cmd.resize(0);
    waveform_cmd_list1.resize(0);
    waveform_cmd_list2.resize(0);
    layer1_draw_list->_ResetForNewFrame();
    layer2_draw_list->_ResetForNewFrame();
    layer3_draw_list->_ResetForNewFrame();
    layer1_draw_list->PushTextureID(font_tex_id);
    layer2_draw_list->PushTextureID(font_tex_id);
    layer3_draw_list->PushTextureID(font_tex_id);
    layer1_draw_list->PushClipRect(view_min, view_max);
    layer2_draw_list->PushClipRect(view_min, view_max);
    layer3_draw_list->PushClipRect(view_min, view_max);

    // Draw guidestripes & grid
    ImVec2 area_size = ImVec2(timeline_width, content_size.y);
    draw_musical_guidestripes(layer1_draw_list, view_min, area_size, scroll_pos_x, view_scale);
    draw_musical_grid(layer1_draw_list, view_min, area_size, scroll_pos_x, inv_view_scale, 1.0f);
  }

  auto& tracks = g_engine.tracks;
  float track_pos_y = timeline_view_pos.y;
  const float expand_max_y = !dragging ? 0.0f : math::max(mouse_pos.y - view_max.y, 0.0f);
  const bool is_mouse_in_selection_range =
      range_selected && math::in_range(mouse_at_time_pos, selection_start_pos, selection_end_pos);

  for (uint32_t i = 0; i < g_engine.tracks.size(); i++) {
    Track* track = tracks[i];
    const float height = track->get_height();
    const float track_view_min_y = offset_y - height - track_separator_height;
    const float expand_min_y = !dragging ? 0.0f : math::max(track_view_min_y - mouse_pos.y, 0.0f);

    // Check track visibility
    if (track_pos_y > view_max.y + expand_max_y) {
      break;
    }

    if (track_pos_y < track_view_min_y - expand_min_y) {
      track_pos_y += height + track_separator_height;
      continue;
    }

    const float next_pos_y = track_pos_y + height;
    ImVec2 track_min(view_min.x, track_pos_y);
    ImVec2 track_max(view_max.x, next_pos_y);
    bool hovering_track_rect = !scrolling && ImGui::IsMouseHoveringRect(track_min, track_max, !dragging);
    bool track_hovered = timeline_hovered && hovering_track_rect;

    // Acquire selection
    if (track_hovered && holding_ctrl && left_mouse_clicked) {
      first_selected_track = i;
      first_selected_track_pos_y = track_pos_y;
      selection_start_pos = mouse_at_gridline;
      selecting_range = true;
    }

    if (track_hovered && selecting_range) {
      last_selected_track = i;
    }

    if (redraw) {
      im_draw_hline(layer1_draw_list, next_pos_y + 0.5f, view_min.x, view_max.x, gridline_color);
    }

    render_track(track, i, track_pos_y, mouse_at_gridline, track_hovered, is_mouse_in_selection_range);

    if (hovering_track_rect && dragging_file) {
      // Highlight drop target
      double highlight_pos = mouse_at_gridline;  // Snap to grid
      double length = 1.0;
      if (drop_payload_data->type == BrowserItem::Sample) {
        length = samples_to_beat(drop_payload_data->content_length, drop_payload_data->sample_rate, beat_duration);
      }

      const double min_pos = highlight_pos * clip_scale;
      const double max_pos = (highlight_pos + length) * clip_scale;
      im_draw_rect_filled(
          layer3_draw_list,
          timeline_scroll_offset_x_f32 + (float)min_pos,
          track_pos_y,
          timeline_scroll_offset_x_f32 + (float)max_pos,
          track_pos_y + height,
          highlight_color);

      // We have file dropped
      if (item_dropped) {
        ClipAddFromFileCmd* cmd = new ClipAddFromFileCmd();
        cmd->track_id = i;
        cmd->cursor_pos = mouse_at_gridline;
        cmd->file = std::move(drop_payload_data->path);
        g_cmd_manager.execute("Add clip from file", cmd);
        Log::info("Dropped at: {}", mouse_at_gridline);
        force_redraw = true;
        recalculate_song_length();
      }
    }

    track_pos_y = next_pos_y + track_separator_height;
  }

  if (redraw) {
    if (edit_command != TimelineCommand::None) {
      render_edited_clips(mouse_at_gridline);
    }
    draw_clips(clip_draw_cmd, sample_scale, offset_y);

    // Draw selection range
    if (selecting_range || range_selected) {
      float track_pos_y = timeline_view_pos.y;
      float selection_start_y = 0.0f;
      float selection_end_y = 0.0f;
      float selection_start_height = 0.0f;
      float selection_end_height = 0.0f;
      int32_t first_track = (int32_t)first_selected_track;
      int32_t last_track = (int32_t)last_selected_track;
      double start_pos = selection_start_pos;
      double end_pos = selection_end_pos;

      if (last_track < first_track) {
        std::swap(first_track, last_track);
      }

      if (edit_command == TimelineCommand::ClipMove || edit_command == TimelineCommand::ClipDuplicate) {
        int32_t num_selected_regions = (int32_t)selected_track_regions.size();
        int32_t max_move = (int32_t)tracks.size() - num_selected_regions;
        int32_t relative_track_offset = (int32_t)hovered_track_id.value() - edit_src_track_id.value();
        double relative_time_offset = mouse_at_gridline - initial_time_pos;
        double selected_time_range = end_pos - start_pos;
        first_track = math::clamp(first_track + relative_track_offset, 0, max_move);
        last_track = (first_track + num_selected_regions) - 1;
        start_pos += relative_time_offset;
        end_pos = start_pos + selected_time_range;
      } else if (edit_command == TimelineCommand::ClipResizeLeft || edit_command == TimelineCommand::ClipResizeRight) {
        double relative_time_offset = mouse_at_gridline - initial_time_pos;
        start_pos = clip_resize_limit;
        end_pos = math::max(clip_resize_pos + relative_time_offset, clip_min_resize_pos);
      }

      for (uint32_t i = 0; i <= last_track; i++) {
        Track* track = g_engine.tracks[i];
        const float height = track->get_height();
        if (selecting_range || range_selected) {
          if (first_track == i) {
            selection_start_y = track_pos_y;
            selection_start_height = height;
          }
          if (last_track == i) {
            selection_end_y = track_pos_y;
            selection_end_height = height;
          }
        }
        track_pos_y += height + track_separator_height;
      }

      static const ImU32 selection_range_fill = ImColor(28, 150, 237, 76);
      static const ImU32 selection_range_border = ImColor(28, 150, 237, 255);
      double min_pos_in_pixel = math::round(start_pos * clip_scale);
      double max_pos_in_pixel = math::round(end_pos * clip_scale);

      if (max_pos_in_pixel < min_pos_in_pixel) {
        std::swap(min_pos_in_pixel, max_pos_in_pixel);
      }

      if (selection_end_y < selection_start_y) {
        selection_start_y += selection_start_height;
        std::swap(selection_start_y, selection_end_y);
      } else {
        selection_end_y += selection_end_height;
      }

      selection_start_rel_y = selection_start_y - timeline_bounds_min_y;
      selection_end_rel_y = selection_end_y - timeline_bounds_min_y;

      const ImVec2 a(timeline_scroll_offset_x_f32 + (float)min_pos_in_pixel, selection_start_y);
      const ImVec2 b(timeline_scroll_offset_x_f32 + (float)max_pos_in_pixel, selection_end_y);

      if (edit_selected) {
        layer3_draw_list->AddRect(a - ImVec2(1.0f, 0.0f), b + ImVec2(1.0f, 1.0f), selection_range_border);
      } else {
        layer3_draw_list->AddRectFilled(a, b, selection_range_fill);
        layer3_draw_list->AddRect(a - ImVec2(1.0f, 0.0f), b + ImVec2(1.0f, 1.0f), selection_range_border);
      }
    }

    layer3_draw_list->PopClipRect();
    layer3_draw_list->PopTextureID();
    layer2_draw_list->PopClipRect();
    layer2_draw_list->PopTextureID();
    layer1_draw_list->PopClipRect();
    layer1_draw_list->PopTextureID();

    ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();
    g_renderer->begin_render(timeline_fb, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    layer_draw_data.Clear();
    layer_draw_data.DisplayPos = view_min;
    layer_draw_data.DisplaySize = timeline_area;
    layer_draw_data.FramebufferScale.x = 1.0f;
    layer_draw_data.FramebufferScale.y = 1.0f;
    layer_draw_data.OwnerViewport = owner_viewport;
    layer_draw_data.AddDrawList(layer1_draw_list);
    g_renderer->render_imgui_draw_data(&layer_draw_data);
    gfx_draw_waveform_batch(waveform_cmd_list1, 0, 0, (int32_t)timeline_area.x, (int32_t)timeline_area.y);

    layer_draw_data.Clear();
    layer_draw_data.DisplayPos = view_min;
    layer_draw_data.DisplaySize = timeline_area;
    layer_draw_data.FramebufferScale.x = 1.0f;
    layer_draw_data.FramebufferScale.y = 1.0f;
    layer_draw_data.OwnerViewport = owner_viewport;
    layer_draw_data.AddDrawList(layer2_draw_list);
    g_renderer->render_imgui_draw_data(&layer_draw_data);
    gfx_draw_waveform_batch(waveform_cmd_list2, 0, 0, (int32_t)timeline_area.x, (int32_t)timeline_area.y);

    layer_draw_data.Clear();
    layer_draw_data.DisplayPos = view_min;
    layer_draw_data.DisplaySize = timeline_area;
    layer_draw_data.FramebufferScale.x = 1.0f;
    layer_draw_data.FramebufferScale.y = 1.0f;
    layer_draw_data.OwnerViewport = owner_viewport;
    layer_draw_data.AddDrawList(layer3_draw_list);
    g_renderer->render_imgui_draw_data(&layer_draw_data);

    g_renderer->end_render();
  }

  if (range_selected && !edit_selected && ((timeline_clicked && left_mouse_clicked) || escape_key_pressed)) {
    selected_track_regions.clear();
    range_selected = false;
    force_redraw = true;
  }

  if (edit_command != TimelineCommand::None) {
    apply_edit(mouse_at_gridline);
  }

  render_clip_context_menu();

  ImTextureID fb_tex_id = (ImTextureID)timeline_fb;
  const ImVec2 fb_image_pos(timeline_view_pos.x, offset_y);
  main_draw_list->AddImage(fb_tex_id, fb_image_pos, fb_image_pos + ImVec2(timeline_width, content_size.y));

  if (g_engine.is_playing()) {
    const double playhead_offset = playhead * inv_view_scale;
    const float playhead_pos = (float)math::round(timeline_view_pos.x - scroll_pos_x + playhead_offset);
    im_draw_vline(main_draw_list, playhead_pos, offset_y, offset_y + timeline_area.y, playhead_color);
  }

  if (range_selected && edit_command == TimelineCommand::None) {
    double min_pos_x = timeline_scroll_offset_x + selection_start_pos * clip_scale;
    double max_pos_x = timeline_scroll_offset_x + selection_end_pos * clip_scale;
    float min_pos_y = timeline_bounds_min_y + selection_start_rel_y + 4.0f;
    float max_pos_y = timeline_bounds_min_y + selection_end_rel_y + 4.0f;

    if (max_pos_x >= view_min.x && min_pos_x < view_max.x && max_pos_y >= view_min.y && min_pos_y < view_max.y) {
      float x = math::clamp((float)math::round(min_pos_x), view_min.x + 4.0f, view_max.x - floating_button_size.x - 4.0f);
      float y = math::min(max_pos_y, view_max.y - 32.0f);
      ImVec2 pos((float)x, y);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

      if (controls::begin_floating_window("Timeline floating buttons", pos)) {
        set_current_font(FontType::Icon);
        ImGui::Button(ICON_MS_MUSIC_NOTE_ADD);
        controls::item_tooltip("Create MIDI clips");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Button(ICON_MS_TIMELINE);
        controls::item_tooltip("Create automation clips");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Button(ICON_MS_REMOVE_SELECTION);
        controls::item_tooltip("Delete region");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Button(ICON_MS_SURGICAL);
        controls::item_tooltip("Slice region");
        set_current_font(FontType::Normal);
        floating_button_size = ImGui::GetWindowSize();
      }

      ImGui::PopStyleVar(2);
      controls::end_floating_window();
    }
  }

  ImGui::PopClipRect();
}

void TimelineWindow::render_track(
    Track* track,
    uint32_t id,
    float track_pos_y,
    double mouse_at_gridline,
    bool track_hovered,
    bool is_mouse_in_selection_range) {
  const float height = track->get_height();
  const bool can_adjust_gain = track->shown || height <= 30.0f;
  double relative_pos = 0.0;
  bool has_clip_selected = false;
  SelectedTrackRegion* selected_region = nullptr;
  TrackClipResizeInfo* clip_resize_info = nullptr;
  bool is_track_selected = math::in_range(id, first_selected_track, last_selected_track);
  is_mouse_in_selection_range = is_mouse_in_selection_range && is_track_selected;

  if (!any_of(edit_command, TimelineCommand::None, TimelineCommand::ClipAdjustGain)) {
    relative_pos = mouse_at_gridline - initial_time_pos;
  }

  if (!selected_track_regions.empty()) {
    uint32_t idx = id - first_selected_track;
    if (idx < selected_track_regions.size()) {
      selected_region = &selected_track_regions[idx];
      has_clip_selected = selected_region->has_clip_selected;
    }
  }

  if (!clip_resize.empty()) {
    uint32_t idx = id - first_selected_track;
    if (idx < clip_resize.size()) {
      clip_resize_info = &clip_resize[idx];
    }
  }

  bool move_or_shift_cmd = any_of(edit_command, TimelineCommand::ClipMove, TimelineCommand::ClipShift);
  bool resize_or_shift_cmd = math::in_range(edit_command, TimelineCommand::ClipResizeLeft, TimelineCommand::ClipShiftRight);
  bool unset_clip_editor = false;

  for (size_t i = 0; i < track->clips.size(); i++) {
    Clip* clip = track->clips[i];

    if (has_deleted_clips && clip->is_deleted())
      continue;

    double min_time = clip->min_time;
    double max_time = clip->max_time;
    double start_offset = clip->start_offset;
    ClipSelectStatus select_status = ClipSelectStatus::NotSelected;

    if (selected_region) {
      select_status = selected_region->is_clip_selected(i);
    }

    if (edit_command != TimelineCommand::None) [[unlikely]] {
      if (edit_selected && redraw && has_clip_selected) [[unlikely]] {
        if (select_status != ClipSelectStatus::NotSelected) {
          if (move_or_shift_cmd) {
            if (select_status == ClipSelectStatus::PartiallySelected) {
              bool is_audio = clip->is_audio();
              bool right_side_partially_selected = selected_region->range.right_side_partially_selected(i);
              bool left_side_partially_selected = selected_region->range.left_side_partially_selected(i);
              const double sample_rate = clip->get_asset_sample_rate();

              if (right_side_partially_selected && left_side_partially_selected) {
                // Carve the center of the clip
                const double resize_offset = max_time + selected_region->range.last_offset - min_time;
                const double lhs_min_time = min_time;
                const double lhs_max_time = clip->min_time + selected_region->range.first_offset;
                const double lhs_start_ofs = start_offset;
                const double rhs_min_time = max_time + selected_region->range.last_offset;
                const double rhs_max_time = max_time;
                const double rhs_start_ofs =
                    calc_clip_shift(is_audio, start_offset, lhs_min_time - rhs_min_time, beat_duration, sample_rate);

                // Draw lhs clip
                render_clip(clip, lhs_min_time, lhs_max_time, lhs_start_ofs, track_pos_y, height);

                // If the current command is ClipShift, draw the shifted portion
                if (edit_command == TimelineCommand::ClipShift) {
                  const double clip_min_time = lhs_max_time;
                  const double clip_max_time = rhs_min_time;
                  const double shift_offset = lhs_min_time - lhs_max_time + relative_pos;
                  double clip_start_offset = calc_clip_shift(
                      clip->is_audio(), start_offset, shift_offset, beat_duration, clip->get_asset_sample_rate());
                  render_clip(clip, clip_min_time, clip_max_time, clip_start_offset, track_pos_y, height);
                }

                // Draw rhs clip
                render_clip(clip, rhs_min_time, rhs_max_time, rhs_start_ofs, track_pos_y, height);
                continue;
              } else if (right_side_partially_selected) {
                // Carve the right side of the clip
                max_time = clip->min_time + selected_region->range.first_offset;
                render_clip(clip, min_time, max_time, start_offset, track_pos_y, height);
                if (edit_command == TimelineCommand::ClipShift) {
                  const double max_time2 = clip->max_time;
                  const double shift_offset = min_time - max_time + relative_pos;
                  const double rhs_start_ofs = calc_clip_shift(
                      clip->is_audio(), start_offset, shift_offset, beat_duration, clip->get_asset_sample_rate());
                  render_clip(clip, max_time, max_time2, rhs_start_ofs, track_pos_y, height);
                }
                continue;
              } else if (left_side_partially_selected) {
                if (edit_command == TimelineCommand::ClipShift) {
                  const double new_start_offset = calc_clip_shift(
                      clip->is_audio(), start_offset, relative_pos, beat_duration, clip->get_asset_sample_rate());
                  render_clip(
                      clip, min_time, max_time + selected_region->range.last_offset, new_start_offset, track_pos_y, height);
                }
                // Carve the left side of the clip
                const double rhs_min_time = max_time + selected_region->range.last_offset;
                const double rhs_max_time = max_time;
                const double rhs_start_ofs =
                    calc_clip_shift(is_audio, start_offset, min_time - rhs_min_time, beat_duration, sample_rate);
                render_clip(clip, rhs_min_time, rhs_max_time, rhs_start_ofs, track_pos_y, height);
                continue;
              }
            } else if (select_status == ClipSelectStatus::Selected) {
              if (edit_command == TimelineCommand::ClipShift) {
                start_offset = calc_clip_shift(
                    clip->is_audio(), start_offset, relative_pos, beat_duration, clip->get_asset_sample_rate());
              } else {
                continue;
              }
            }
          } else if (resize_or_shift_cmd) {
            if (clip_resize_info && clip_resize_info->should_resize && clip_resize_info->clip_id == i) {
              continue;
            }
          }
        }
      } else if (clip == edited_clip && edit_command != TimelineCommand::ClipDuplicate) {
        continue;
      }
    }

    const double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
    const double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;
    const float min_pos_x_in_pixel = (float)math::round(min_pos_x);
    const float max_pos_x_in_pixel = (float)math::round(max_pos_x);

    // Check clip visibility
    if (min_pos_x_in_pixel >= timeline_bounds_max_x)
      break;
    if (max_pos_x_in_pixel < timeline_bounds_min_x)
      continue;

    const ImVec2 min_bb(min_pos_x_in_pixel, track_pos_y);
    const ImVec2 max_bb(max_pos_x_in_pixel, track_pos_y + height);
    bool should_edit_selected = false;
    ClipHover current_hover_state{};

    if (track_hovered && edit_command == TimelineCommand::None && !holding_ctrl) [[unlikely]] {
      static constexpr float handle_offset = 4.0f;
      ImRect clip_rect(min_bb, max_bb);
      // Hitboxes for sizing handle
      ImRect left_handle(min_pos_x_in_pixel, track_pos_y, min_pos_x_in_pixel + handle_offset, max_bb.y);
      ImRect right_handle(max_pos_x_in_pixel - handle_offset, track_pos_y, max_pos_x_in_pixel, max_bb.y);

      // Triggers command
      if (left_handle.Contains(mouse_pos)) {
        if (left_mouse_clicked) {
          if (!holding_alt) {
            edit_command = TimelineCommand::ClipResizeLeft;
          } else {
            edit_command = TimelineCommand::ClipShiftLeft;
          }
          should_edit_selected = prepare_resize_for_selected_range(clip, false);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        current_hover_state = ClipHover::LeftHandle;
      } else if (right_handle.Contains(mouse_pos)) {
        if (left_mouse_clicked) {
          if (!holding_alt) {
            edit_command = TimelineCommand::ClipResizeRight;
          } else {
            edit_command = TimelineCommand::ClipShiftRight;
          }
          should_edit_selected = prepare_resize_for_selected_range(clip, true);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        current_hover_state = ClipHover::RightHandle;
      } else if (clip_rect.Contains(mouse_pos)) {
        float gain_ctrl_pos_x = math::max(min_pos_x_in_pixel, timeline_view_pos.x) + 4.0f;
        float gain_ctrl_pos_y = track_pos_y + height - 17.0f;
        ImRect gain_ctrl(gain_ctrl_pos_x, gain_ctrl_pos_y, gain_ctrl_pos_x + 50.0f, gain_ctrl_pos_y + 13.0f);

        if (can_adjust_gain && clip->is_audio() && gain_ctrl.Contains(mouse_pos)) {
          if (left_mouse_clicked) {
            if (!holding_alt) {
              current_value = math::linear_to_db(clip->audio.gain);
              edit_command = TimelineCommand::ClipAdjustGain;
            } else {
              ClipAdjustGainCmd* cmd = new ClipAdjustGainCmd();
              cmd->track_id = id;
              cmd->clip_id = clip->id;
              cmd->gain_before = clip->audio.gain;
              cmd->gain_after = 1.0f;
              g_cmd_manager.execute("Reset clip gain", cmd);
              force_redraw = true;
            }
          }
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        } else {
          if (left_mouse_clicked) {
            if (holding_shift) {
              edit_command = TimelineCommand::ClipDuplicate;
            } else if (holding_alt) {
              edit_command = TimelineCommand::ClipShift;
            } else {
              edit_command = TimelineCommand::ClipMove;
            }
            should_edit_selected = is_mouse_in_selection_range;
            unset_clip_editor = false;
            g_clip_editor.set_clip(id, clip->id);
          } else if (right_mouse_clicked) {
            edit_command = TimelineCommand::ShowClipContextMenu;
          }
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }

        current_hover_state = ClipHover::All;
      }

      if (left_mouse_clicked && current_hover_state == ClipHover::None) {
        unset_clip_editor = true;
      }

      if (edit_command != TimelineCommand::None) {
        initial_time_pos = mouse_at_gridline;
        hovered_track_id = id;
        edit_selected = should_edit_selected;
        edited_track = track;
        edit_src_track_id = id;
        edited_track_pos_y = track_pos_y;
        edited_clip = should_edit_selected ? nullptr : clip;
        continue;
      }
    }

    if (clip->hover_state != current_hover_state) {
      clip->hover_state = current_hover_state;
      force_redraw = true;
    }

    if (redraw) {
      bool shown_in_clip_editor = g_clip_editor.current_track == track && g_clip_editor.current_clip == clip;
      ClipDrawCmd* cmd = clip_draw_cmd.emplace_back_raw();
      cmd->type = clip->type;
      cmd->hover_state = clip->hover_state;
      cmd->clip = clip;
      cmd->start_offset = start_offset;
      cmd->min_pos_x = min_pos_x;
      cmd->max_pos_x = max_pos_x;
      cmd->min_pos_y = track_pos_y;
      cmd->height = height;
      cmd->draw_flags = shown_in_clip_editor ? ClipDrawCmd::Highlighted : 0;

      if (clip->is_audio()) {
        cmd->gain = clip->audio.gain;
        cmd->audio = clip->audio.asset->peaks;
      } else {
        cmd->gain = 0.0f;
        cmd->midi = &clip->midi.asset->data;
      }
    }
  }

  if (track_hovered && left_mouse_clicked && !track->has_clips()) {
    unset_clip_editor = true;
  }

  if (unset_clip_editor) {
    g_clip_editor.unset_clip();
    force_redraw = true;
  }

  if (track->input_attr.recording) {
    const double min_pos_x = math::round(timeline_scroll_offset_x + track->record_min_time * clip_scale);
    const double max_pos_x = math::round(timeline_scroll_offset_x + track->record_max_time * clip_scale);
    const float min_clamped_pos_x = (float)math::max(min_pos_x, (double)timeline_bounds_min_x);
    const float max_clamped_pos_x = (float)math::min(max_pos_x, (double)timeline_bounds_max_x);
    im_draw_rect_filled(
        layer3_draw_list, min_clamped_pos_x, track_pos_y, max_clamped_pos_x, track_pos_y + height, highlight_color);
    layer2_draw_list->AddText(ImVec2(min_clamped_pos_x + 4.0f, track_pos_y + 2.0f), text_transparent_color, "Recording...");
  }
}

void TimelineWindow::render_edited_clips(double mouse_at_gridline) {
  const double relative_pos = mouse_at_gridline - initial_time_pos;

  if (edited_clip) {
    float track_pos_y = edited_track_pos_y;
    float track_height = edited_track->get_height();
    double min_time = edited_clip->min_time;
    double max_time = edited_clip->max_time;
    double start_offset = edited_clip->start_offset;

    switch (edit_command) {
      case TimelineCommand::ClipDuplicate:
      case TimelineCommand::ClipMove: {
        const uint32_t track_id = hovered_track_id.value();
        auto [new_min_time, new_max_time] = calc_move_clip(edited_clip, relative_pos);
        min_time = new_min_time;
        max_time = new_max_time;
        track_pos_y = get_track_position_y(track_id);
        track_height = g_engine.tracks[track_id]->get_height();
        break;
      }
      case TimelineCommand::ClipResizeLeft: {
        const double min_length = 1.0 / grid_scale;
        auto [new_min_time, new_max_time, new_start_offset] =
            calc_resize_clip(edited_clip, relative_pos, edited_clip->max_time, min_length, 0.0, beat_duration, true);
        start_offset = new_start_offset;
        min_time = new_min_time;
        break;
      }
      case TimelineCommand::ClipResizeRight: {
        const double min_length = 1.0 / grid_scale;
        auto [new_min_time, new_max_time, new_start_offset] =
            calc_resize_clip(edited_clip, relative_pos, edited_clip->min_time, min_length, 0.0, beat_duration, false);
        max_time = new_max_time;
        break;
      }
      case TimelineCommand::ClipShiftLeft: {
        const double min_length = 1.0 / grid_scale;
        auto [new_min_time, new_max_time, rel_offset] =
            calc_resize_clip(edited_clip, relative_pos, edited_clip->max_time, min_length, 0.0, beat_duration, true, true);
        start_offset = rel_offset;
        min_time = new_min_time;
        break;
      }
      case TimelineCommand::ClipShiftRight: {
        const double min_length = 1.0 / grid_scale;
        auto [new_min_time, new_max_time, rel_offset] =
            calc_resize_clip(edited_clip, relative_pos, edited_clip->min_time, min_length, 0.0, beat_duration, false, true);
        start_offset = rel_offset;
        max_time = new_max_time;
        break;
      }
      case TimelineCommand::ClipShift: {
        start_offset = shift_clip_content(edited_clip, relative_pos, beat_duration);
        break;
      }
      case TimelineCommand::ClipAdjustGain: {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        current_value += drag_delta.y * -0.1f;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        float gain_value = math::db_to_linear(current_value);
        g_engine.set_clip_gain(edited_track, edited_clip->id, gain_value);
        break;
      }
      default: break;
    }

    render_clip(edited_clip, min_time, max_time, start_offset, track_pos_y, track_height, true);
  } else if (edit_selected) {
    if (edit_command == TimelineCommand::ClipShift) {
      return;
    }

    double min_length = 1.0 / grid_scale;
    int32_t first_track = first_selected_track;
    int32_t move_offset = 0;

    if (any_of(edit_command, TimelineCommand::ClipMove, TimelineCommand::ClipDuplicate)) {
      int32_t track_size = (int32_t)g_engine.tracks.size();
      int32_t src_track = edit_src_track_id.value();
      int32_t min_move = src_track - (int32_t)first_selected_track;
      int32_t max_move = track_size - ((int32_t)last_selected_track - src_track) - 1;
      move_offset = math::clamp(hovered_track_id.value(), min_move, max_move) - src_track;
      first_track = first_track + move_offset;
    }

    float track_pos_y = get_track_position_y(first_track);
    for (int32_t i = first_selected_track; i <= (int32_t)last_selected_track; i++) {
      Track* src_track = g_engine.tracks[i];
      Track* dst_track = g_engine.tracks[i + move_offset];
      const float height = dst_track->get_height();
      const float track_view_min_y = timeline_bounds_min_y - height - track_separator_height;

      if (track_pos_y > timeline_bounds_max_x) {
        break;
      }

      const SelectedTrackRegion& selected_region = selected_track_regions[i - first_selected_track];
      if (track_pos_y < track_view_min_y || !selected_region.has_clip_selected) {
        track_pos_y += height + track_separator_height;
        continue;
      }

      double min_move = 0.0;
      if (any_of(edit_command, TimelineCommand::ClipMove, TimelineCommand::ClipDuplicate)) {
        // Display substitute clips on carved regions
        for (uint32_t j = selected_region.range.first; j <= selected_region.range.last; j++) {
          Clip* clip = src_track->clips[j];
          double min_time = clip->min_time;
          double max_time = clip->max_time;
          double start_offset = clip->start_offset;
          bool right_side_partially_selected = selected_region.range.right_side_partially_selected(j);
          bool left_side_partially_selected = selected_region.range.left_side_partially_selected(j);

          if (right_side_partially_selected && left_side_partially_selected) {
            const double new_min_time = min_time + selected_region.range.first_offset;
            const double min_time_moved = math::max(new_min_time + relative_pos, min_move);
            const double length = (max_time - new_min_time) + selected_region.range.last_offset;
            const double max_time_moved = min_time_moved + length;
            const double new_start_ofs = calc_clip_shift(
                clip->is_audio(), start_offset, min_time - new_min_time, beat_duration, clip->get_asset_sample_rate());
            min_time = min_time_moved;
            max_time = max_time_moved;
            start_offset = new_start_ofs;
          } else if (right_side_partially_selected) {
            const double new_min_time = min_time + selected_region.range.first_offset;
            const double min_time_moved = math::max(new_min_time + relative_pos, min_move);
            const double max_time_moved = min_time_moved + (max_time - new_min_time);
            const double new_start_ofs = calc_clip_shift(
                clip->is_audio(), start_offset, min_time - new_min_time, beat_duration, clip->get_asset_sample_rate());
            min_time = min_time_moved;
            max_time = max_time_moved;
            min_move = max_time_moved;
            start_offset = new_start_ofs;
          } else if (left_side_partially_selected) {
            const double new_max_time = max_time + selected_region.range.last_offset;
            const double min_time_moved = math::max(min_time + relative_pos, min_move);
            const double max_time_moved = min_time_moved + (new_max_time - min_time);
            min_time = min_time_moved;
            max_time = max_time_moved;
            min_move = max_time_moved;
          } else [[likely]] {
            const auto [new_min_time, new_max_time] = calc_move_clip(clip, relative_pos, min_move);
            min_time = new_min_time;
            max_time = new_max_time;
            min_move = new_max_time;
          }

          const double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
          const double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;
          const float min_pos_x_in_pixel = (float)math::round(min_pos_x);
          const float max_pos_x_in_pixel = (float)math::round(max_pos_x);

          if (min_pos_x_in_pixel >= timeline_bounds_max_x)
            break;
          if (max_pos_x_in_pixel < timeline_bounds_min_x)
            continue;

          ClipDrawCmd* cmd = clip_draw_cmd.emplace_back_raw();
          cmd->type = clip->type;
          cmd->hover_state = ClipHover::None;
          cmd->clip = clip;
          cmd->start_offset = start_offset;
          cmd->min_pos_x = min_pos_x;
          cmd->max_pos_x = max_pos_x;
          cmd->min_pos_y = track_pos_y;
          cmd->height = height;
          cmd->draw_flags = ClipDrawCmd::Layer2;

          if (clip->is_audio()) {
            cmd->gain = clip->audio.gain;
            cmd->audio = clip->audio.asset->peaks;
          } else {
            cmd->gain = 0.0f;
            cmd->midi = &clip->midi.asset->data;
          }
        }
      } else if (edit_command >= TimelineCommand::ClipResizeLeft && edit_command <= TimelineCommand::ClipShiftRight) {
        bool shift_mode = edit_command == TimelineCommand::ClipShiftLeft || edit_command == TimelineCommand::ClipShiftRight;
        bool left_side = edit_command == TimelineCommand::ClipResizeLeft || edit_command == TimelineCommand::ClipShiftLeft;
        const TrackClipResizeInfo& clip_resize_info = clip_resize[i - first_selected_track];
        if (clip_resize_info.should_resize) {
          Clip* clip = src_track->clips[clip_resize_info.clip_id];
          auto [new_min_time, new_max_time, new_start_offset] = calc_resize_clip(
              clip,
              relative_pos,
              clip_resize_limit,
              min_length,
              clip_min_resize_pos,
              beat_duration,
              left_side,
              shift_mode,
              true);
          render_clip(clip, new_min_time, new_max_time, new_start_offset, track_pos_y, height, true);
        }
      }

      track_pos_y += height + track_separator_height;
    }
  }
}

void TimelineWindow::render_clip(
    Clip* clip,
    double min_time,
    double max_time,
    double start_offset,
    float track_pos_y,
    float height,
    uint32_t draw_flags) {
  const double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
  const double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;
  const float min_pos_x_in_pixel = (float)math::round(min_pos_x);
  const float max_pos_x_in_pixel = (float)math::round(max_pos_x);
  if (max_pos_x_in_pixel >= timeline_bounds_min_x && min_pos_x_in_pixel < timeline_bounds_max_x) {
    ClipDrawCmd* cmd = clip_draw_cmd.emplace_back_raw();
    cmd->type = clip->type;
    cmd->hover_state = ClipHover::None;
    cmd->clip = clip;
    cmd->start_offset = start_offset;
    cmd->min_pos_x = min_pos_x;
    cmd->max_pos_x = max_pos_x;
    cmd->min_pos_y = track_pos_y;
    cmd->height = height;
    cmd->draw_flags = draw_flags;

    if (clip->is_audio()) {
      cmd->gain = clip->audio.gain;
      cmd->audio = clip->audio.asset->peaks;
    } else {
      cmd->gain = 0.0f;
      cmd->midi = &clip->midi.asset->data;
    }
  }
}

void TimelineWindow::draw_clips(const Vector<ClipDrawCmd>& clip_cmd_list, double sample_scale, float offset_y) {
  constexpr ImDrawListFlags draw_list_aa_flags =
      ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedLines;

  Color text_col(text_color);
  ImVec2 half(0.5f, 0.5f);
  const ImVec4& rect = layer1_draw_list->_ClipRectStack.back();
  const float font_size = font->FontSize;

  for (auto& cmd : clip_cmd_list) {
    static constexpr float border_contrast_ratio = 1.0f / 3.5f;
    static constexpr float text_contrast_ratio = 1.0f / 1.57f;
    static constexpr double log_base4 = 1.0 / 1.3862943611198906;  // 1.0 / log(4.0)

    Clip* clip = cmd.clip;
    const Color color(clip->color);
    const float bg_contrast_ratio = calc_contrast_ratio(color, text_color);
    const Color border_color =
        (bg_contrast_ratio > border_contrast_ratio) ? Color(0.0f, 0.0f, 0.0f, 0.3f) : Color(1.0f, 1.0f, 1.0f, 0.2f);
    Color text_color_adjusted =
        (bg_contrast_ratio > text_contrast_ratio) ? Color(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.45f) : text_col;

    const double start_offset = cmd.start_offset;
    const bool is_active = cmd.clip->is_active();
    const float min_draw_x = rect.x;
    const float min_pos_x = (float)math::round(cmd.min_pos_x) + 0.5f;
    const float max_pos_x = (float)math::round(cmd.max_pos_x) + 0.5f;
    const float min_pos_clamped_x = math::max(min_pos_x, rect.x - 3.0f);
    const float max_pos_clamped_x = math::min(max_pos_x, rect.z + 3.0f);
    const float min_pos_y = cmd.min_pos_y;
    const float height = cmd.height;
    const float clip_title_max_y = min_pos_y + font_size + 4.0f;
    const ImVec2 clip_title_min_bb(min_pos_clamped_x, min_pos_y);
    const ImVec2 clip_title_max_bb(max_pos_clamped_x, clip_title_max_y);
    const ImVec2 clip_content_min(min_pos_clamped_x, clip_title_max_y);
    const ImVec2 clip_content_max(max_pos_clamped_x, min_pos_y + height);
    const float darkening = is_active ? 0.80f : 0.60f;
    const Color base_color = is_active ? color : color.desaturate(0.4f);
    const Color bg_color = base_color.change_alpha(base_color.a * darkening).premult_alpha();
    const Color content_color = is_active ? base_color.brighten(1.2f) : base_color.brighten(0.5f);
    const bool draw_in_layer2 = has_bit(cmd.draw_flags, ClipDrawCmd::Layer2);
    const bool highlighted = has_bit(cmd.draw_flags, ClipDrawCmd::Highlighted);
    const bool show_content = cmd.height > 30.0f;
    auto* dl = !draw_in_layer2 ? layer1_draw_list : layer2_draw_list;

    if (draw_in_layer2) {
      // Add small shadow border
      dl->AddRect(clip_title_min_bb, clip_content_max, 0x3F000000, 3.0f, ImDrawFlags_RoundCornersTop, 4.5f);
    }

    dl->AddRectFilled(clip_title_min_bb, clip_content_max, bg_color.to_uint32(), 3.0f, ImDrawFlags_RoundCornersTop);

    if (!highlighted) {
      dl->AddRect(clip_title_min_bb - half, clip_content_max + half, 0x3F000000, 3.0f, ImDrawFlags_RoundCornersTop);
    } else {
      layer2_draw_list->AddRect(
          clip_title_min_bb - half,
          clip_content_max + half,
          content_color.to_uint32(),
          3.0f,
          ImDrawFlags_RoundCornersTop,
          1.5f);
    }

    if (!is_active) {
      text_color_adjusted = text_color_adjusted.change_alpha(0.75f);
    }

    // Draw clip label
    if (clip->name.size() != 0) {
      const char* str = clip->name.c_str();
      static constexpr float label_padding_x = 5.0f;
      const float label_padding_y = show_content ? 2.0f : (height - font_size) * 0.5f;
      const ImVec2 label_pos(std::max(clip_title_min_bb.x, rect.x) + label_padding_x, min_pos_y + label_padding_y);
      const ImVec4 clip_label_rect(clip_title_min_bb.x, clip_title_min_bb.y, clip_title_max_bb.x - 6.0f, clip_content_max.y);
      dl->AddText(font, font_size, label_pos, text_color, str, str + clip->name.size(), 0.0f, &clip_label_rect);
    }

    if (show_content) {
      switch (clip->type) {
        case ClipType::Audio: {
          SampleAsset* asset = clip->audio.asset;
          if (asset) {
            WaveformVisual* waveform = cmd.audio;
            if (!waveform)
              break;
            const double scale_x = sample_scale * (double)waveform->sample_rate;
            const double inv_scale_x = 1.0 / scale_x;
            double mip_index = std::log(scale_x * 0.5) * log_base4;  // Scale -> Index
            const int32_t index = math::clamp((int32_t)mip_index, 0, waveform->mipmap_count - 1);
            double mip_scale = std::pow(4.0, mip_index - (double)index) * 2.0;  // Index -> Mip Scale
            // const double mip_index = (std::log(scale_x * 0.5) * log_base4) * 0.5; // Scale -> Index
            // const int32_t index = math::clamp((int32_t)mip_index, 0, sample_peaks->mipmap_count - 1);
            // const double mult = std::pow(4.0, (double)index - 1.0);
            // const double mip_scale =
            //     std::pow(4.0, 2.0 * (mip_index - (double)index)) * 8.0 * mult; // Index -> Mip Scale
            // const double mip_div = math::round(scale_x / mip_scale);

            const double waveform_len = ((double)waveform->sample_count - start_offset) * inv_scale_x;
            const double rel_min_x = cmd.min_pos_x - (double)min_draw_x;
            const double rel_max_x = cmd.max_pos_x - (double)min_draw_x;
            const double min_pos_x = math::max(rel_min_x, 0.0);
            const double max_pos_x =
                math::min(math::min(rel_max_x, rel_min_x + waveform_len), (double)(timeline_width + 2.0));
            const double draw_count = math::max(max_pos_x - min_pos_x, 0.0);
            const double length = rel_max_x - rel_min_x;
            const float gap_size = (float)(length / std::floor(length));

            // Log::debug("{} {} {}", index, mip_scale, (double)sample_peaks->sample_count / mip_index);
            /*Log::debug("{} {} {} {} {}", sample_peaks->sample_count / (size_t)mip_div, mip_div, index,
                       math::round(start_offset / mip_div), mip_scale);*/

            if (draw_count) {
              auto& waveform_cmd_list = !draw_in_layer2 ? waveform_cmd_list1 : waveform_cmd_list2;
              double waveform_start = start_offset * inv_scale_x;
              const double start_idx = std::round(math::max(-rel_min_x, 0.0) + waveform_start);
              const float min_bb_x = (float)math::round(min_pos_x);
              const float max_bb_x = (float)math::round(max_pos_x);
              const float pos_y = clip_content_min.y - offset_y;
              const ColorU32 waveform_color = content_color.to_uint32();
              if (waveform->channels == 2) {
                const float height = std::floor((clip_content_max.y - clip_content_min.y) * 0.5f);
                waveform_cmd_list.push_back({
                  .waveform_vis = waveform,
                  .min_x = min_bb_x,
                  .min_y = pos_y,
                  .max_x = max_bb_x,
                  .max_y = pos_y + height,
                  .gain = cmd.gain,
                  .scale_x = (float)mip_scale,
                  .gap_size = gap_size,
                  .color = waveform_color,
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
                  .gain = cmd.gain,
                  .scale_x = (float)mip_scale,
                  .gap_size = gap_size,
                  .color = waveform_color,
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
                  .gain = cmd.gain,
                  .scale_x = (float)mip_scale,
                  .gap_size = gap_size,
                  .color = waveform_color,
                  .mip_index = index,
                  .start_idx = (uint32_t)start_idx,
                  .draw_count = (uint32_t)draw_count + 2,
                });
              }
            }
          }
          break;
        }
        case ClipType::Midi: {
          constexpr float min_note_size_px = 2.5f;
          constexpr float max_note_size_px = 10.0f;
          constexpr uint32_t min_note_range = 4;
          const MidiAsset* asset = clip->midi.asset;
          const uint32_t min_note = asset->data.min_note;
          const uint32_t max_note = asset->data.max_note;
          uint32_t note_range = (asset->data.max_note + 1) - min_note;

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

          const float min_view = math::max(min_pos_clamped_x, min_draw_x);
          const float max_view = math::min(max_pos_clamped_x, min_draw_x + timeline_width);
          const ColorU32 note_color = content_color.to_uint32();

          if (asset) {
            uint32_t channel_count = asset->data.channel_count;
            double min_start_x = cmd.min_pos_x - start_offset * clip_scale;
            for (uint32_t i = 0; i < channel_count; i++) {
              for (uint32_t j = 0; const auto& note : asset->data.note_sequence) {
                float min_pos_x = (float)math::round(min_start_x + note.min_time * clip_scale);
                float max_pos_x = (float)math::round(min_start_x + note.max_time * clip_scale);
                if (max_pos_x < min_view)
                  continue;
                if (min_pos_x > max_view)
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
          }
          break;
        }
        default: break;
      }

      layer3_draw_list->PushClipRect(clip_content_min, clip_content_max);

      if (clip->is_audio()) {
        ImVec2 content_rect_min = layer3_draw_list->GetClipRectMin();
        float ctrl_pos_x = math::max(clip_content_min.x, content_rect_min.x);
        float width = max_pos_clamped_x - ctrl_pos_x;
        float gain = cmd.gain;

        if (!math::near_equal(gain, 1.0f) || cmd.hover_state == ClipHover::All) {
          char gain_str[8]{};
          float gain_db = math::linear_to_db(gain);
          fmt::format_to(gain_str, "{:.1f}db", gain_db);

          constexpr float min_width = 60.0f;
          float alpha = (width >= min_width) ? 1.0f : width / min_width;
          ImVec2 ctrl_pos(ctrl_pos_x + 4.0f, clip_content_max.y - 16.0f);
          draw_clip_overlay(ctrl_pos, 50.0f, alpha, bg_color, gain_str);
        }
      }

      layer3_draw_list->PopClipRect();
    }

    if (clip->hover_state != ClipHover::None) {
      switch (clip->hover_state) {
        case ClipHover::LeftHandle: {
          ImVec2 min_bb(min_pos_x, min_pos_y);
          ImVec2 max_bb(max_pos_x, min_pos_y + height);
          layer3_draw_list->AddLine(
              ImVec2(min_bb.x + 0.5f, min_bb.y),
              ImVec2(min_bb.x + 0.5f, max_bb.y),
              ImGui::GetColorU32(ImGuiCol_ButtonActive),
              3.0f);
          break;
        }
        case ClipHover::RightHandle: {
          ImVec2 min_bb(min_pos_x, min_pos_y);
          ImVec2 max_bb(max_pos_x, min_pos_y + height);
          layer3_draw_list->AddLine(
              ImVec2(max_bb.x - 1.5f, min_bb.y),
              ImVec2(max_bb.x - 1.5f, max_bb.y),
              ImGui::GetColorU32(ImGuiCol_ButtonActive),
              3.0f);
          break;
        }
        default: break;
      }
    }
  }
}

void TimelineWindow::draw_clip_overlay(ImVec2 pos, float size, float alpha, const Color& col, const char* caption) {
  ImU32 ctrl_bg = col.darken(0.8f).to_uint32();
  ImVec2 text_size = ImGui::CalcTextSize(caption);
  float text_offset_x = 0.5f * (size - text_size.x);
  uint32_t bg_alpha = (uint32_t)(199.0f * alpha) << 24;
  uint32_t caption_alpha = (uint32_t)(255.0f * alpha) << 24;
  im_draw_box_filled(layer3_draw_list, pos.x, pos.y, size, 13.0f, (ctrl_bg & 0x00FF'FFFF) | bg_alpha, 3.0f);
  layer3_draw_list->AddText(ImVec2(pos.x + text_offset_x, pos.y), 0x00FF'FFFF | caption_alpha, caption);
}

void TimelineWindow::apply_edit(double mouse_at_gridline) {
  if (timeline_window_focused && edit_command != TimelineCommand::None && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    finish_edit();
    force_redraw = true;
    return;
  }

  double relative_pos = mouse_at_gridline - initial_time_pos;

  if (!edit_selected) {
    switch (edit_command) {
      case TimelineCommand::ClipMove:
        if (!left_mouse_down) {
          if (relative_pos != 0.0 || edit_src_track_id.value() != hovered_track_id.value()) {
            ClipMoveCmd* cmd = new ClipMoveCmd();
            cmd->src_track_id = edit_src_track_id.value();
            cmd->dst_track_id = hovered_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->relative_pos = relative_pos;
            g_cmd_manager.execute("Move Clip", cmd);
            g_clip_editor.unset_clip();
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
      case TimelineCommand::ClipResizeLeft:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd* cmd = new ClipResizeCmd();
            cmd->track_id = edit_src_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->left_side = true;
            cmd->relative_pos = relative_pos;
            cmd->min_length = 1.0 / grid_scale;
            cmd->last_beat_duration = beat_duration;
            g_cmd_manager.execute("Resize clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      case TimelineCommand::ClipResizeRight:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd* cmd = new ClipResizeCmd();
            cmd->track_id = edit_src_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->left_side = false;
            cmd->relative_pos = relative_pos;
            cmd->min_length = 1.0 / grid_scale;
            cmd->last_beat_duration = beat_duration;
            g_cmd_manager.execute("Resize clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      case TimelineCommand::ClipShiftLeft:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd* cmd = new ClipResizeCmd();
            cmd->track_id = edit_src_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->left_side = true;
            cmd->shift = true;
            cmd->relative_pos = relative_pos;
            cmd->min_length = 1.0 / grid_scale;
            cmd->last_beat_duration = beat_duration;
            g_cmd_manager.execute("Resize and shift clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      case TimelineCommand::ClipShiftRight:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd* cmd = new ClipResizeCmd();
            cmd->track_id = edit_src_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->left_side = false;
            cmd->shift = true;
            cmd->relative_pos = relative_pos;
            cmd->min_length = 1.0 / grid_scale;
            cmd->last_beat_duration = beat_duration;
            g_cmd_manager.execute("Resize and shift clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      case TimelineCommand::ClipShift:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipShiftCmd* cmd = new ClipShiftCmd();
            cmd->track_id = edit_src_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->relative_pos = relative_pos;
            cmd->last_beat_duration = beat_duration;
            g_cmd_manager.execute("Shift clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
      case TimelineCommand::ClipDuplicate:
        if (!left_mouse_down) {
          if (relative_pos != 0.0 || edit_src_track_id.value() != hovered_track_id.value()) {
            ClipDuplicateCmd* cmd = new ClipDuplicateCmd();
            cmd->src_track_id = edit_src_track_id.value();
            cmd->dst_track_id = hovered_track_id.value();
            cmd->clip_id = edited_clip->id;
            cmd->relative_pos = relative_pos;
            g_cmd_manager.execute("Duplicate clip", cmd);
            g_clip_editor.unset_clip();
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
      case TimelineCommand::ClipAdjustGain:
        if (!left_mouse_down) {
          ClipAdjustGainCmd* cmd = new ClipAdjustGainCmd();
          cmd->track_id = edit_src_track_id.value();
          cmd->clip_id = edited_clip->id;
          cmd->gain_before = edited_clip->audio.gain;
          cmd->gain_after = math::db_to_linear(current_value);
          g_cmd_manager.execute("Adjust clip gain", cmd);
          finish_edit();
        }
        break;
      case TimelineCommand::ShowClipContextMenu:
        ImGui::OpenPopup("clip_context_menu");
        context_menu_track_id = edit_src_track_id.value();
        context_menu_track = edited_track;
        context_menu_clip = edited_clip;
        tmp_color = edited_clip->color;
        tmp_name = edited_clip->name;
        finish_edit();
        break;
      default: finish_edit(); break;
    }
  } else {
    switch (edit_command) {
      case TimelineCommand::ClipMove:
      case TimelineCommand::ClipDuplicate:
        if (!left_mouse_down) {
          if (relative_pos != 0.0 || edit_src_track_id.value() != hovered_track_id.value()) {
            int32_t track_size = (int32_t)g_engine.tracks.size();
            int32_t src_track = edit_src_track_id.value();
            int32_t min_move = src_track - (int32_t)first_selected_track;
            int32_t max_move = track_size - ((int32_t)last_selected_track - src_track) - 1;
            ClipMoveCmd2* cmd = new ClipMoveCmd2();
            cmd->selected_track_regions = selected_track_regions;
            cmd->src_track_idx = first_selected_track;
            cmd->dst_track_relative_idx = math::clamp(hovered_track_id.value(), min_move, max_move) - src_track;
            cmd->min_pos = selection_start_pos;
            cmd->max_pos = selection_end_pos;
            cmd->relative_move_pos = relative_pos;
            cmd->duplicate = edit_command == TimelineCommand::ClipDuplicate;
            g_cmd_manager.execute(cmd->duplicate ? "Duplicate clip" : "Move clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
      case TimelineCommand::ClipResizeLeft:
      case TimelineCommand::ClipResizeRight:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd2* cmd = new ClipResizeCmd2();
            cmd->track_clip = clip_resize;
            cmd->first_track = first_selected_track;
            cmd->relative_pos = relative_pos;
            cmd->resize_limit = clip_resize_limit;
            cmd->min_length = 1.0 / grid_scale;
            cmd->min_resize_pos = clip_min_resize_pos;
            cmd->right_side = edit_command == TimelineCommand::ClipResizeRight;
            cmd->shift = false;
            g_cmd_manager.execute("Resize clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      case TimelineCommand::ClipShiftLeft:
      case TimelineCommand::ClipShiftRight:
        if (!left_mouse_down) {
          if (relative_pos != 0.0) {
            ClipResizeCmd2* cmd = new ClipResizeCmd2();
            cmd->track_clip = clip_resize;
            cmd->first_track = first_selected_track;
            cmd->relative_pos = relative_pos;
            cmd->resize_limit = clip_resize_limit;
            cmd->min_length = 1.0 / grid_scale;
            cmd->right_side = edit_command == TimelineCommand::ClipShiftRight;
            cmd->shift = true;
            g_cmd_manager.execute("Shift clip", cmd);
          }
          finish_edit();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        break;
      default: break;
    }
  }
}

void TimelineWindow::query_selected_range() {
  selected_track_regions.reserve((last_selected_track - first_selected_track) + 1);
  for (uint32_t i = first_selected_track; i <= last_selected_track; i++) {
    Track* track = g_engine.tracks[i];
    auto query_result = track->query_clip_by_range(selection_start_pos, selection_end_pos);
    selected_track_regions.push_back({
      .has_clip_selected = query_result.has_value(),
      .range = query_result ? query_result.value() : ClipQueryResult{},
    });
  }

  Log::debug("---- Track selected ----");
  Log::debug("Selected range: {} -> {}", selection_start_pos, selection_end_pos);
  for (auto& sel : selected_track_regions) {
    Log::debug("Track: {} -> {} ({} -> {})", sel.range.first, sel.range.last, sel.range.first_offset, sel.range.last_offset);
  }
}

bool TimelineWindow::prepare_resize_for_selected_range(Clip* src_clip, bool dir) {
  if (selected_track_regions.empty()) {
    return false;
  }

  double resize_pos = dir ? src_clip->max_time : src_clip->min_time;
  if (selection_start_pos > resize_pos || selection_end_pos < resize_pos) {
    return false;
  }

  double resize_limit = dir ? 0.0 : DBL_MAX;
  double min_resize_pos = 0.0;
  // Find clip that matches the resize position based on resize direction
  for (uint32_t i = first_selected_track; i <= last_selected_track; i++) {
    Track* track = g_engine.tracks[i];
    const SelectedTrackRegion& selected_region = selected_track_regions[i - first_selected_track];
    uint32_t first_clip = selected_region.range.first;
    uint32_t last_clip = selected_region.range.last;
    bool should_resize = false;
    uint32_t clip_id = 0;

    if (selected_region.has_clip_selected) {
      if (selected_region.range.first != selected_region.range.last) {
        if (!dir) {
          if (selected_region.range.first_offset > 0.0) {
            first_clip++;
          }
        } else {
          if (selected_region.range.last_offset < 0.0) {
            last_clip--;
          }
        }
        for (uint32_t j = first_clip; j <= last_clip; j++) {
          Clip* clip = track->clips[j];
          double time_pos = dir ? clip->max_time : clip->min_time;
          if (time_pos == resize_pos) {
            should_resize = true;
            clip_id = j;
            if (dir) {
              resize_limit = math::max(resize_limit, clip->min_time);
            } else {
              double start_offset = clip->get_start_offset(beat_duration);
              resize_limit = math::min(resize_limit, clip->max_time);
              min_resize_pos = math::max(min_resize_pos, time_pos - start_offset);
            }
            break;
          }
        }
      } else {
        Clip* clip = track->clips[selected_region.range.first];
        if (!dir) {
          if (clip->min_time == resize_pos && selected_region.range.first_offset < 0.0) {
            double start_offset = clip->get_start_offset(beat_duration);
            should_resize = true;
            clip_id = clip->id;
            resize_limit = math::min(resize_limit, clip->max_time);
            min_resize_pos = math::max(min_resize_pos, clip->min_time - start_offset);
          }
        } else {
          if (clip->max_time == resize_pos && selected_region.range.last_offset > 0.0) {
            should_resize = true;
            clip_id = clip->id;
            resize_limit = math::max(resize_limit, clip->min_time);
          }
        }
      }
    }

    clip_resize.push_back({
      .should_resize = should_resize,
      .clip_id = clip_id,
    });
  }

  clip_resize_limit = resize_limit;
  clip_resize_pos = resize_pos;
  clip_min_resize_pos = min_resize_pos;
  return true;
}

float TimelineWindow::get_track_position_y(uint32_t id) {
  uint32_t track_count = (uint32_t)g_engine.tracks.size();
  if (id == 0 || track_count == 0) {
    return timeline_view_pos.y;
  }
  if (id >= track_count) {
    id = track_count - 1;
  }
  float track_pos_y = timeline_view_pos.y;
  for (uint32_t i = 0; i < id; i++) {
    Track* track = g_engine.tracks[i];
    track_pos_y += track->get_height() + track_separator_height;
  }
  return track_pos_y;
}

void TimelineWindow::recalculate_song_length() {
  double max_length = g_engine.get_song_length();
  if (max_length > 100.0) {
    max_length += 32;
    min_hscroll = min_hscroll * song_length / max_length;
    max_hscroll = max_hscroll * song_length / max_length;
    song_length = max_length;
  } else {
    min_hscroll = min_hscroll * song_length / 100.0;
    max_hscroll = max_hscroll * song_length / 100.0;
    song_length = 100.0;
  }
}

void TimelineWindow::finish_edit() {
  hovered_track = nullptr;
  hovered_track_y = 0.0f;
  hovered_track_id = {};
  hovered_track_height = 60.0f;
  edited_clip = nullptr;
  edited_track = nullptr;
  edited_track_pos_y = 0.0f;
  edit_selected = false;
  range_selected = false;
  force_redraw = true;
  edit_command = TimelineCommand::None;
  current_value = 0.0f;
  initial_time_pos = 0.0;
  clip_resize_limit = 0.0;
  clip_min_resize_pos = 0.0;
  clip_resize.resize_fast(0);
  recalculate_song_length();
  selected_track_regions.clear();
  Log::debug("Finish edit");
}

void TimelineWindow::add_track() {
  TrackAddCmd* cmd = new TrackAddCmd();
  cmd->color = Color::from_hsv((float)color_spin / 15.0f, 0.6172f, 0.80f);
  g_cmd_manager.execute("Add track", cmd);
  color_spin = (color_spin + 1) % 15;
  redraw = true;
}

void TimelineWindow::add_plugin(Track* track, PluginUID uid) {
  PluginInterface* plugin = g_engine.add_plugin_to_track(track, uid);
  if (!plugin)
    return;
  if (plugin->has_view())
    wm_add_foreign_plugin_window(plugin);
}

}  // namespace wb