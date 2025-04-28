#include "clip_editor.h"

#include <imgui.h>

#include <optional>

#include "IconsMaterialSymbols.h"
#include "command_manager.h"
#include "controls.h"
#include "core/color.h"
#include "engine/engine.h"
#include "file_dialog.h"
#include "font.h"
#include "grid.h"
#include "timeline.h"
#include "window.h"

#define WB_GRID_SIZE_HEADER_TUPLETS (const char*)1
#define WB_GRID_SIZE_HEADER_BARS    (const char*)2

#ifndef NDEBUG
#define WB_ENABLE_PIANO_ROLL_DEBUG_MENU
#endif

namespace wb {

static const char* note_scale[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

// -1.0 is dummy value
static double grid_mult_table[] = {
  0.0, -1.0, 1.0 / 2.0, 1.0 / 3.0, 1.0 / 4.0, 1.0 / 6.0, 1.0 / 8.0, 1.0 / 16.0, 1.0 / 32.0, -1.0, 1.0, 2.0, 4.0, 8.0,
};

static const char* grid_size_table[] = {
  "Off",       WB_GRID_SIZE_HEADER_TUPLETS, "1/2 beat", "1/3 beat", "1/4 beat", "1/6 beat", "1/8 beat", "1/16 beat",
  "1/32 beat", WB_GRID_SIZE_HEADER_BARS,    "1 bar",    "2 bars",   "4 bars",   "8 bars",
};

ClipEditorWindow::ClipEditorWindow() {
  vsplitter_size = 70.0f;
  vsplitter_min_size = 70.0f;
}

void ClipEditorWindow::init() {
  g_cmd_manager.add_on_history_update_listener([this] { force_redraw = true; });
  layer1_dl = new ImDrawList(ImGui::GetDrawListSharedData());
  layer2_dl = new ImDrawList(ImGui::GetDrawListSharedData());
}

void ClipEditorWindow::shutdown() {
  delete layer1_dl;
  delete layer2_dl;
  if (piano_roll_fb)
    g_renderer->destroy_texture(piano_roll_fb);
}

void ClipEditorWindow::set_clip(uint32_t track_id, uint32_t clip_id) {
  current_track = g_engine.tracks[track_id];
  current_clip = current_track->clips[clip_id];
  current_track_id = track_id;
  current_clip_id = clip_id;
  force_redraw = true;
}

void ClipEditorWindow::unset_clip() {
  current_track = {};
  current_clip = {};
  current_track_id.reset();
  current_clip_id.reset();
  force_redraw = true;
}

bool ClipEditorWindow::contains_clip() {
  return current_track != nullptr && current_clip != nullptr;
}

void ClipEditorWindow::open_midi_file() {
  if (auto file = open_file_dialog({ { "Standard MIDI File", "mid" } })) {
    // load_notes_from_file(midi_note, file.value());
  }
}

void ClipEditorWindow::render() {
  ppq = g_engine.ppq;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  if (!controls::begin_window("Clip Editor", &g_piano_roll_window_open)) {
    ImGui::PopStyleVar();
    controls::end_window();
    return;
  }
  ImGui::PopStyleVar();

  if (current_track == nullptr && current_clip == nullptr) {
    controls::end_window();
    return;
  }

  if (!current_clip->is_midi()) {
    controls::end_window();
    return;
  }

  redraw = force_redraw;
  if (force_redraw) {
    force_redraw = false;
  }

  bool note_height_changed = false;
  if (note_height != new_note_height) {
    note_height = new_note_height;
    note_height_changed = true;
  }

  font = ImGui::GetFont();
  note_height_in_pixel = math::round(note_height);

  if (ImGui::BeginChild("PianoRollControl", ImVec2(200.0f, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_MenuBar)) {
    if (ImGui::BeginMenuBar()) {
      const char* track_name = current_track->name.empty() ? "<unnamed track>" : current_track->name.c_str();
      const char* clip_name = current_clip->name.empty() ? "<unnamed clip>" : current_clip->name.c_str();
      ImGui::Text("%s - %s", clip_name, track_name);
      ImGui::EndMenuBar();
    }

    const ImVec4 selected_tool_color =
        ImGui::ColorConvertU32ToFloat4(Color(ImGui::GetStyleColorVec4(ImGuiCol_Button)).brighten(0.15f).to_uint32());

    if (current_clip->is_midi()) {
      const char* grid_size_text = nullptr;
      ImFormatStringToTempBuffer(&grid_size_text, nullptr, "Snap to grid: %s", grid_size_table[grid_mode]);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::BeginCombo("##grid_size", grid_size_text, ImGuiComboFlags_HeightLarge)) {
        for (uint32_t i = 0; auto type : grid_size_table) {
          if (type == WB_GRID_SIZE_HEADER_TUPLETS) {
            ImGui::SeparatorText("Tuplets");
          } else if (type == WB_GRID_SIZE_HEADER_BARS) {
            ImGui::SeparatorText("Bars");
          } else {
            if (ImGui::Selectable(type, grid_mode == i)) {
              grid_mode = i;
            }
          }
          i++;
        }
        ImGui::EndCombo();
      }
      ImGui::Checkbox("Preview", &preview_note);

      ImGui::Separator();

      bool draw_tool = piano_roll_tool == PianoRollTool::Draw;
      bool marker_tool = piano_roll_tool == PianoRollTool::Marker;
      bool paint_tool = piano_roll_tool == PianoRollTool::Paint;
      bool slice_tool = piano_roll_tool == PianoRollTool::Slice;
      bool erase_tool = piano_roll_tool == PianoRollTool::Erase;
      bool mute_tool = piano_roll_tool == PianoRollTool::Mute;

      set_current_font(FontType::Icon);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));

      if (controls::toggle_button(ICON_MS_STYLUS "##pr_draw", draw_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Draw;
      }
      controls::item_tooltip("Draw tool");
      ImGui::SameLine(0.0f, 0.0f);

      if (controls::toggle_button(ICON_MS_INK_HIGHLIGHTER_MOVE "##pr_marker", marker_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Marker;
      }
      controls::item_tooltip("Marker tool");
      ImGui::SameLine(0.0f, 0.0f);

      if (controls::toggle_button(ICON_MS_DRAW "##pr_paint", paint_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Paint;
      }
      controls::item_tooltip("Paint tool");
      ImGui::SameLine(0.0f, 0.0f);

      if (controls::toggle_button(ICON_MS_SPLIT_SCENE "##pr_slice", slice_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Slice;
      }
      controls::item_tooltip("Slice tool");
      ImGui::SameLine(0.0f, 0.0f);

      if (controls::toggle_button(ICON_MS_INK_ERASER "##pr_erase", erase_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Erase;
      }
      controls::item_tooltip("Erase tool");
      ImGui::SameLine(0.0f, 0.0f);

      if (controls::toggle_button(ICON_MS_MUSIC_OFF "##pr_mute", mute_tool, selected_tool_color)) {
        piano_roll_tool = PianoRollTool::Mute;
      }
      controls::item_tooltip("Mute tool");

      ImGui::PopStyleVar(2);
      set_current_font(FontType::Normal);

      ImGui::Checkbox("Use last selected note", &use_last_note);
      ImGui::PushItemWidth(-FLT_MIN);
      if (any_of(piano_roll_tool, PianoRollTool::Draw, PianoRollTool::Marker, PianoRollTool::Paint)) {
        switch (piano_roll_tool) {
          case PianoRollTool::Draw: ImGui::TextUnformatted("Draw tool:"); break;
          case PianoRollTool::Marker: ImGui::TextUnformatted("Marker tool:"); break;
          case PianoRollTool::Paint: ImGui::TextUnformatted("Paint tool:"); break;
        }

        ImGui::SliderInt("##note_ch", &new_channel, 1, 16, "Note channel: %d");
        ImGui::SliderFloat("##note_velocity", &new_velocity, 0.0f, 127.0f, "Note velocity: %.1f");

        if (piano_roll_tool != PianoRollTool::Marker) {
          ImGui::DragFloat(
              "##note_length", &new_length, 0.1f, 0.125f, 8.0f, "Note length: %.1f bar", ImGuiSliderFlags_Vertical);
        }

        /*const char* note_length_str;
        if (new_length < 0) {
          ImFormatStringToTempBuffer(&note_length_str, nullptr, "Note length: 1/%d beat", math::abs(new_length));
        } else {
          ImFormatStringToTempBuffer(
              &note_length_str, nullptr, new_length == 1 ? "Note length: %d beat" : "Note length: %d beats", new_length);
        }*/
        /*int old_length = new_length;
        if (ImGui::DragInt(
                "##note_length",
                &new_length,
                0.25f,
                -32,
                8,
                note_length_str,
                ImGuiSliderFlags_Vertical | ImGuiSliderFlags_NoInput)) {
          if (old_length == 1 && old_length > new_length) {
            new_length = -2;
          } else if (old_length == -2 && old_length < new_length) {
            new_length = 1;
          }
        }*/

        if (piano_roll_tool == PianoRollTool::Paint) {
          ImGui::Checkbox("Lock pitch", &lock_pitch);
        }
      }
      ImGui::PopItemWidth();

#ifdef WB_ENABLE_PIANO_ROLL_DEBUG_MENU
      ImGui::Checkbox("Display note id", &display_note_id);
      ImGui::Text("Num selected: %d", current_clip->get_midi_data()->num_selected);
#endif
    }
    ImGui::EndChild();
  }

  ImGui::SameLine(0.0f, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  if (ImGui::BeginChild("PianoRoll", ImVec2(), ImGuiChildFlags_AlwaysUseWindowPadding)) {
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    render_horizontal_scrollbar();
    double new_time_pos = 0.0;
    if (render_time_ruler(&new_time_pos)) {
      // TODO
    }
    ImGui::PopStyleVar();

    ImVec2 content_origin = ImGui::GetCursorScreenPos();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float view_height = child_content_size.y;

    child_content_size = ImGui::GetContentRegionAvail();
    main_cursor_pos = cursor_pos;
    im_draw_hline(
        draw_list,
        content_origin.y - 1.0f,
        content_origin.x,
        content_origin.x + child_content_size.x,
        ImGui::GetColorU32(ImGuiCol_Separator));

    content_height = child_content_size.y * (1.0f - space_divider);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());

    if (note_height_changed) {
      ImGui::SetNextWindowScroll(ImVec2(0.0f, last_scroll_pos_y_normalized * note_height_in_pixel * note_count));
    }

    if (ImGui::BeginChild("PianoRollContent", ImVec2(0.0f, content_height))) {
      const Color base_color = current_clip->color;
      indicator_frame_color = base_color.change_alpha(0.5f).darken(0.6f).to_uint32();
      indicator_color = base_color.darken(0.6f).to_uint32();
      note_color = base_color.brighten(0.75f).change_alpha(0.85f).to_uint32();
      text_color = base_color.darken(1.5f).to_uint32();
      piano_roll_dl = ImGui::GetWindowDrawList();
      vscroll = ImGui::GetScrollY();

      ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
      if (scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
        ImGui::SetScrollY(vscroll - scroll_delta_y);
        scroll_delta_y = 0.0f;
        redraw = true;
      }

    if ((last_vscroll - vscroll) != 0.0f)
      redraw = true;

    float separator_x = cursor_pos.x + vsplitter_min_size + 0.5f;
    piano_roll_dl->AddLine(
        ImVec2(separator_x, cursor_pos.y),
        ImVec2(separator_x, cursor_pos.y + content_height),
        ImGui::GetColorU32(ImGuiCol_Separator),
        2.0f);

      render_note_keys();
      render_note_editor();
    }
    ImGui::EndChild();

    if (controls::hsplitter(
            ImGui::GetID("##PIANO_ROLL_SEPARATOR"),
            &content_height,
            0.25f * child_content_size.y,
            0.0f,
            child_content_size.y)) {
      space_divider = 1.0 - (content_height / child_content_size.y);
    }
    ImGui::PopStyleVar();

    ImGui::BeginChild("##PIANO_ROLL_EVENT");
    render_event_editor();
    ImGui::EndChild();

    ImGui::EndChild();
  }

  controls::end_window();
}

void ClipEditorWindow::render_note_keys() {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(
      "PianoRollKeys",
      ImVec2(vsplitter_min_size, note_count * note_height_in_pixel),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  ImGui::SameLine(0.0f, 2.0f);

  if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
    ImVec2 pos = ImGui::GetMousePos();
    GImGui->ColorPickerRef.x = pos.x;
    GImGui->ColorPickerRef.y = pos.y;
    wm_set_mouse_pos((int)pos.x, (int)pos.y);
    wm_reset_relative_mouse_state();
    wm_enable_relative_mouse_mode(true);
    zoom_pos_y = pos.y - cursor_pos.y;
    zooming_vertically = true;
  }

  if (zooming_vertically) {
    int x, y;
    wm_get_relative_mouse_state(&x, &y);
    if (y != 0) {
      zoom_vertically(zoom_pos_y, note_count * note_height_in_pixel, (float)y * 0.1f);
    }
  }

  if (zooming_vertically && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    zooming_vertically = false;
    wm_enable_relative_mouse_mode(false);
    wm_set_mouse_pos((int)GImGui->ColorPickerRef.x, (int)GImGui->ColorPickerRef.y);
  }

  // Draw piano keys
  float keys_height = note_count_per_oct * note_height_in_pixel;
  float oct_pos_y = main_cursor_pos.y - std::fmod(vscroll, keys_height);
  ImVec2 oct_pos = ImVec2(cursor_pos.x, oct_pos_y);
  uint32_t oct_count = (uint32_t)std::ceil(content_height / keys_height);
  int oct_scroll_offset = (uint32_t)(max_oct_count - std::floor(vscroll / keys_height)) - oct_count - 1;
  for (int i = oct_count; i >= 0; i--) {
    int oct_offset = i + oct_scroll_offset;
    if (oct_offset < 0)
      break;
    draw_piano_keys(piano_roll_dl, oct_pos, ImVec2(vsplitter_min_size, note_height_in_pixel), oct_offset);
  }
}

void ClipEditorWindow::render_note_editor() {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 region_size = ImGui::GetContentRegionAvail();
  timeline_width = region_size.x;

  double view_scale = calc_view_scale();
  double inv_view_scale = 1.0 / view_scale;
  float max_height = note_count * note_height_in_pixel;
  float offset_y = vscroll + cursor_pos.y;
  ImVec2 view_min(cursor_pos.x, offset_y);
  ImVec2 view_max(cursor_pos.x + timeline_width, offset_y + region_size.y);
  ImGui::PushClipRect(view_min, view_max, true);

  ImGui::InvisibleButton("PianoRollContent", ImVec2(region_size.x, max_height));

  // Resize piano roll framebuffer
  if (old_piano_roll_size.x != region_size.x || old_piano_roll_size.y != region_size.y) {
    int width = (int)math::max(region_size.x, 16.0f);
    int height = (int)math::max(region_size.y, 16.0f);
    if (piano_roll_fb)
      g_renderer->destroy_texture(piano_roll_fb);
    piano_roll_fb = g_renderer->create_texture(
        GPUTextureUsage::Sampled | GPUTextureUsage::RenderTarget,
        GPUFormat::UnormB8G8R8A8,
        width,
        height,
        true,
        0,
        0,
        nullptr);
    assert(piano_roll_fb != nullptr);
    Log::debug("Piano roll framebuffer resized ({}x{})", (int)width, (int)height);
    old_piano_roll_size = region_size;
    redraw = redraw || true;
  }

  ImVec2 mouse_pos = ImGui::GetMousePos();
  float mouse_wheel = ImGui::GetIO().MouseWheel;
  float mouse_wheel_h = ImGui::GetIO().MouseWheelH;
  bool is_active = ImGui::IsItemActive();
  bool is_activated = ImGui::IsItemActivated();
  bool is_piano_roll_hovered = ImGui::IsItemHovered();
  bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);

  holding_shift = ImGui::IsKeyDown(ImGuiKey_ModShift);
  holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
  holding_alt = ImGui::IsKeyDown(ImGuiKey_ModAlt);

  if (is_piano_roll_hovered && mouse_wheel_h != 0.0f) {
    scroll_horizontal(mouse_wheel_h, song_length, -view_scale * 64.0);
  }

  // Assign scroll
  if (middle_mouse_clicked && middle_mouse_down && is_piano_roll_hovered)
    scrolling = true;

  // Do scroll
  if (scrolling) {
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
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

  if (edit_command != PianoRollTool::None || selecting_notes) {
    static constexpr float speed = 0.1f;
    static constexpr float drag_offset_x = 20.0f;
    static constexpr float drag_offset_y = 40.0f;
    float min_offset_x = view_min.x;
    float max_offset_x = view_max.x;
    float min_offset_y = view_min.y;
    float max_offset_y = view_max.y;

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
    redraw = true;
  }

  ImVec2 area_size = ImVec2(timeline_width, region_size.y);
  ImU32 guidestrip_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.13f).to_uint32();
  ImU32 grid_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.55f).to_uint32();
  double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
  double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
  double clip_scale = inv_view_scale;
  double hovered_position = 0.0;
  double hovered_position_grid = 0.0;

  hovered_key = MidiData::max_keys - (int32_t)((mouse_pos.y - cursor_pos.y) / note_height_in_pixel) - 1;
  if (is_piano_roll_hovered || is_active) {
    hovered_position = ((double)(mouse_pos.x - cursor_pos.x) * view_scale + min_hscroll * song_length);
    hovered_position_grid = std::round(hovered_position * (double)grid_scale) / (double)grid_scale;
  }

  // Start selection
  if (holding_ctrl && is_activated && edit_command == PianoRollTool::None) {
    selection_start_pos = hovered_position;
    first_selected_key = hovered_key;
    append_selection = holding_shift;
    selecting_notes = true;
  }

  auto midi_asset = current_clip->midi.asset;

  // Release selection
  if (!is_active && selecting_notes) {
    selection_end_pos = hovered_position;
    last_selected_key = hovered_key;
    if (last_selected_key < first_selected_key) {
      std::swap(last_selected_key, first_selected_key);
    }
    if (selection_end_pos < selection_start_pos) {
      std::swap(selection_start_pos, selection_end_pos);
    }

    if (append_selection) {
      Vector<uint32_t> note_ids =
          midi_asset->data.find_notes(selection_start_pos, selection_end_pos, first_selected_key, last_selected_key, 0);
      if (note_ids.size() != 0) {
        MidiAppendNoteSelectionCmd* cmd = new MidiAppendNoteSelectionCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->select_or_deselect = true;
        cmd->selected_note_ids = std::move(note_ids);
        g_cmd_manager.execute("Clip editor: Append note selection", cmd);

        // Recalculate minimum and maximum move bounds
        uint32_t first_note = note_ids[0];
        uint32_t num_selected = midi_asset->data.num_selected;
        const MidiNoteBuffer& note_seq = midi_asset->data.note_sequence;
        min_note_pos = note_seq[first_note].min_time;
        min_note_key = UINT16_MAX;
        max_note_key = 0;
        for (const auto& note : note_seq) {
          min_note_key = math::min(min_note_key, note.key);
          max_note_key = math::max(max_note_key, note.key);
        }
      }
    } else {
      MidiSelectNoteCmd* cmd = new MidiSelectNoteCmd();
      cmd->track_id = current_track_id.value();
      cmd->clip_id = current_clip_id.value();
      cmd->min_pos = selection_start_pos;
      cmd->max_pos = selection_end_pos;
      cmd->min_key = first_selected_key;
      cmd->max_key = last_selected_key;
      g_cmd_manager.execute("Clip editor: Select/deselect note", cmd);
      if (!cmd->result.selected.empty()) {
        uint32_t first_note = cmd->result.selected[0];
        MidiNote& note = midi_asset->data.note_sequence[first_note];
        min_note_pos = note.min_time;
        min_note_key = cmd->result.min_key;
        max_note_key = cmd->result.max_key;
      }
    }

    selecting_notes = false;
    append_selection = false;
  }

  // Update selection bounds
  if (selecting_notes) {
    selection_end_pos = hovered_position;
    last_selected_key = hovered_key;
  }

  bool notes_selected = midi_asset->data.num_selected > 0;
  double min_move_pos = initial_time_pos - min_note_pos;
  int32_t min_key_move = initial_key - min_note_key;
  int32_t max_key_move = MidiData::max_keys - (max_note_key - initial_key) - 1;
  int32_t relative_key_pos = 0;
  double relative_pos = 0.0;

  if (edit_command == PianoRollTool::Move) {
    relative_pos = math::max(hovered_position_grid, min_move_pos) - initial_time_pos;
    relative_key_pos = math::clamp(hovered_key, min_key_move, max_key_move) - initial_key;
  }

  // Release action
  if (!is_active && edit_command != PianoRollTool::None) {
    // NOTE(native-m):
    // Any edits will not be applied until the action is released.
    switch (edit_command) {
      case PianoRollTool::Draw: {
        MidiAddNoteCmd* cmd = new MidiAddNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->min_time = hovered_position_grid;
        cmd->max_time = hovered_position_grid + new_length;
        cmd->velocity = new_velocity / 127.0f;
        cmd->note_key = hovered_key;
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Draw tool", cmd);
        break;
      }
      case PianoRollTool::Marker: {
        MidiAddNoteCmd* cmd = new MidiAddNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->min_time = initial_time_pos;
        cmd->max_time = math::max(hovered_position_grid, initial_time_pos);
        cmd->velocity = new_velocity / 127.0f;
        cmd->note_key = initial_key;
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Marker tool", cmd);
        break;
      }
      case PianoRollTool::Paint: {
        MidiPaintNotesCmd* cmd = new MidiPaintNotesCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->notes = std::move(painted_notes);
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Paint tool", cmd);
        break;
      }
      case PianoRollTool::Move: {
        if (relative_pos != 0.0 || relative_key_pos != 0) {
          MidiMoveNoteCmd* cmd = new MidiMoveNoteCmd();
          cmd->track_id = current_track_id.value();
          cmd->clip_id = current_clip_id.value();
          cmd->note_id = edited_note_id;
          cmd->move_selected = notes_selected;
          cmd->relative_pos = relative_pos;
          cmd->relative_key_pos = relative_key_pos;
          g_cmd_manager.execute("Clip editor: Move tool", cmd);
        }
        break;
      }
    }
    redraw = true;
    g_timeline.redraw_screen();
    edit_command = PianoRollTool::None;
    initial_time_pos = 0.0;
    initial_key = -1;
    min_paint = 1;
    max_paint = INT32_MIN;
    edited_note_id = WB_INVALID_NOTE_ID;
    fg_notes.resize_fast(0);
    if (!painted_notes.empty()) {
      painted_notes.resize(0);
    }
  }

  if (redraw) {
    ImTextureID font_tex_id = ImGui::GetIO().Fonts->TexID;
    layer1_dl->_ResetForNewFrame();
    layer2_dl->_ResetForNewFrame();
    layer1_dl->PushTextureID(font_tex_id);
    layer2_dl->PushTextureID(font_tex_id);
    layer1_dl->PushClipRect(view_min, view_max);
    layer2_dl->PushClipRect(view_min, view_max);
    fg_notes.resize_fast(0);

    // Draw guidestripes & grid
    draw_musical_guidestripes(layer1_dl, view_min, area_size, scroll_pos_x, view_scale);
    draw_musical_grid(layer1_dl, view_min, area_size, scroll_pos_x, inv_view_scale, 0.5f);

    // Draw horizontal gridlines
    float key_pos_y = main_cursor_pos.y - std::fmod(vscroll, note_height_in_pixel);
  int num_keys = (int)math::min(math::round(content_height / note_height_in_pixel), note_count);
  int key_index_offset = (int)(vscroll / note_height_in_pixel);
  ImVec2 key_pos = ImVec2(cursor_pos.x, key_pos_y - 1.0f);
    for (int i = 0; i <= num_keys; i++) {
      uint32_t index = i + key_index_offset;
      uint32_t note_semitone = index % 12;
      layer1_dl->AddLine(key_pos, key_pos + ImVec2(timeline_width, 0.0f), grid_color);

      if (note_semitone / 7) {
        note_semitone++;
      }

      if (note_semitone % 2 == 0) {
        layer1_dl->AddRectFilled(
            key_pos + ImVec2(0.0f, 1.0f), key_pos + ImVec2(timeline_width, note_height_in_pixel), guidestrip_color);
      }

      key_pos.y += note_height_in_pixel;
    }
  }

  auto font = ImGui::GetFont();
  float font_size = font->FontSize;
  float half_font_size = font_size * 0.5f;
  float half_note_size = note_height_in_pixel * 0.5f;
  float end_x = cursor_pos.x + timeline_width;
  float end_y = main_cursor_pos.y + content_height;
  ImU32 handle_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
  bool start_command = !holding_ctrl && is_activated && left_mouse_clicked && edit_command == PianoRollTool::None;
  std::optional<uint32_t> hovered_note_id;

  auto draw_note =
      [&]<bool WithCommand>(
          float min_pos_x, float max_pos_x, float vel, uint32_t note_id, uint16_t key, uint16_t flags = 0) -> PianoRollTool {
    float pos_y = (float)(131 - key) * note_height_in_pixel;
    float min_pos_y = cursor_pos.y + pos_y;
    float max_pos_y = min_pos_y + note_height_in_pixel;

    ImVec2 a(min_pos_x + 0.5f, min_pos_y);
    ImVec2 b(max_pos_x + 0.5f, max_pos_y - 0.5f);
    if (a.y > end_y || b.y < main_cursor_pos.y)
      return PianoRollTool::None;

    if (redraw) {
#ifdef WB_ENABLE_PIANO_ROLL_DEBUG_MENU
      if (display_note_id) {
        char str_id[16]{};
        fmt::format_to_n(str_id, sizeof(str_id), "{}", note_id);
        layer1_dl->AddText(ImVec2(a.x, a.y - font_size), 0xFFFFFFFF, str_id);
      }
#endif

      bool selected = contain_bit(flags, MidiNoteFlags::Selected);

      // Draw note rect
      layer1_dl->PathLineTo(a);
      layer1_dl->PathLineTo(ImVec2(b.x, a.y));
      layer1_dl->PathLineTo(b);
      layer1_dl->PathLineTo(ImVec2(a.x, b.y));
      layer1_dl->PathFillConvex(note_color);

      // Draw note border
      layer1_dl->PathLineTo(a);
      layer1_dl->PathLineTo(ImVec2(b.x, a.y));
      layer1_dl->PathLineTo(b);
      layer1_dl->PathLineTo(ImVec2(a.x, b.y));
      layer1_dl->PathStroke(!selected ? 0x44000000 : 0xFFFFFFFF, ImDrawFlags_Closed, !selected ? 1.0f : 2.0f);

      if (note_height_in_pixel > 13.0f) {
        float note_text_padding_y;
        if (note_height_in_pixel > 22.0f) {
          // Draw velocity indicator
          float indicator_width = max_pos_x - min_pos_x - 5.0f;
          if (indicator_width > 1.0f) {
            im_draw_box_filled(layer1_dl, min_pos_x + 3.0f, max_pos_y - 7.0f, indicator_width, 4.0f, indicator_frame_color);
            im_draw_box_filled(layer1_dl, min_pos_x + 3.0f, max_pos_y - 7.0f, indicator_width * vel, 4.0f, indicator_color);
          }
          note_text_padding_y = 2.0f;
        } else {
        note_text_padding_y = half_note_size - half_font_size;
      }

      // Draw note pitch
      ImVec4 label_rect(a.x, a.y, b.x - 4.0f, b.y);
        char note_name[5]{};
        const char* scale = note_scale[key % 12];
        fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, key / 12);
        layer1_dl->AddText(
            font,
            font_size,
            ImVec2(std::max(cursor_pos.x, min_pos_x) + 3.0f, a.y + note_text_padding_y),
          text_color,
          note_name,
          nullptr,
            0.0f,
            &label_rect);
      }
    }

    PianoRollTool command{};
    if constexpr (WithCommand) {
      if (holding_ctrl) {
        return PianoRollTool::None;
      }

      ImRect note_rect(min_pos_x, min_pos_y, max_pos_x, max_pos_y);
      if (is_piano_roll_hovered && note_rect.Contains(mouse_pos)) {
        static constexpr float handle_offset = 4.0f;
        ImRect left_handle(min_pos_x, min_pos_y, min_pos_x + handle_offset, max_pos_y);
        ImRect right_handle(max_pos_x - handle_offset, min_pos_y, max_pos_x, max_pos_y);
        if (left_handle.Contains(mouse_pos)) {
          layer1_dl->AddRectFilled(left_handle.Min, left_handle.Max, handle_color);
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          command = PianoRollTool::ResizeLeft;
        } else if (right_handle.Contains(mouse_pos)) {
          layer1_dl->AddRectFilled(right_handle.Min, right_handle.Max, handle_color);
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          command = PianoRollTool::ResizeRight;
        } else if (piano_roll_tool != PianoRollTool::Slice) {
          command = PianoRollTool::Move;
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        hovered_note_id = note_id;
      }
    }

    return command;
  };

  double sel_start_pos = 0.0;
  double sel_end_pos = 0.0;
  uint32_t sel_first_key = 0;
  uint32_t sel_last_key = 0;

  if (selecting_notes) {
    sel_start_pos = math::min(selection_start_pos, selection_end_pos);
    sel_end_pos = math::max(selection_start_pos, selection_end_pos);
    sel_first_key = math::max(first_selected_key, last_selected_key);
    sel_last_key = math::min(first_selected_key, last_selected_key);
  }

  uint32_t note_id = 0;
  for (const auto& note : midi_asset->data.note_sequence) {
    uint16_t flags = note.flags;
    bool selected = contain_bit(flags, MidiNoteFlags::Selected);

    if (edit_command == PianoRollTool::Move && (selected || note_id == edited_note_id)) {
      fg_notes.push_back(note_id);
      note_id++;
      continue;
    }

    float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * clip_scale);
    float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * clip_scale);

    if (max_pos_x < cursor_pos.x) {
      note_id++;
      continue;
    }

    if (min_pos_x > end_x)
      break;

    if (selecting_notes) {
      if (!append_selection) {
        flags &= ~MidiNoteFlags::Selected;  // The note may selected, make it appear deselected
      }
      if (note.min_time <= sel_end_pos && note.max_time >= sel_start_pos && note.key >= sel_last_key &&
          note.key <= sel_first_key) {
        if (append_selection && selected) {
          flags &= ~MidiNoteFlags::Selected;
        } else {
          flags |= MidiNoteFlags::Selected;
        }
      }
    }

    // Can't directly call with function call syntax :'(
    if (auto cmd = draw_note.operator()<true>(min_pos_x, max_pos_x, note.velocity, note_id, note.key, flags);
        start_command && cmd != PianoRollTool::None) {
      edit_command = cmd;
      initial_time_pos = hovered_position_grid;
      initial_key = hovered_key;
      edited_note_id = note_id;
      if (!notes_selected) {
        min_note_pos = note.min_time;
        min_note_key = note.key;
        max_note_key = note.key;
      }
      if (notes_selected && !selected) {
        select_or_deselect_all_notes(false);
        notes_selected = false;
      }
    }

    note_id++;
  }

  // Register command
  if (!holding_ctrl && is_activated && left_mouse_clicked && edit_command == PianoRollTool::None) {
    if (notes_selected) {
      select_or_deselect_all_notes(false);
      notes_selected = false;
    }
    if (piano_roll_tool == PianoRollTool::Slice) {
      MidiSliceNoteCmd* cmd = new MidiSliceNoteCmd();
      cmd->track_id = current_track_id.value();
      cmd->clip_id = current_clip_id.value();
      cmd->pos = hovered_position_grid;
      cmd->velocity = new_velocity / 127.0f;
      cmd->note_key = hovered_key;
      cmd->channel = 0;
      g_cmd_manager.execute("Clip editor: Slice tool", cmd);
      g_timeline.redraw_screen();
      force_redraw = true;
    } else {
      edit_command = piano_roll_tool;
      initial_time_pos = hovered_position_grid;
      initial_key = hovered_key;
    }
  }

  // Process command
  if (edit_command == PianoRollTool::Draw) {
    uint16_t key = math::clamp(hovered_key, 0, (int32_t)MidiData::max_keys);
    double min_time = math::max(hovered_position_grid, 0.0);
    double max_time = hovered_position_grid + new_length;
    float min_pos_x = (float)math::round(scroll_offset_x + min_time * clip_scale);
    float max_pos_x = (float)math::round(scroll_offset_x + max_time * clip_scale);
    draw_note.operator()<false>(min_pos_x, max_pos_x, new_velocity / 127.0f, 0, key);
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  } else if (edit_command == PianoRollTool::Marker) {
    double min_time = math::max(initial_time_pos, 0.0);
    double max_time = math::max(hovered_position_grid, min_time);
    float min_pos_x = (float)math::round(scroll_offset_x + min_time * clip_scale);
    float max_pos_x = (float)math::round(scroll_offset_x + max_time * clip_scale);
    draw_note.operator()<false>(min_pos_x, max_pos_x, new_velocity / 127.0f, 0, initial_key);
  } else if (edit_command == PianoRollTool::Paint) {
    uint16_t key = lock_pitch ? initial_key : math::clamp(hovered_key, 0, (int32_t)MidiData::max_keys);
    double relative_pos = hovered_position_grid - initial_time_pos;
    int32_t paint_pos = (int32_t)std::floor(relative_pos / (double)new_length);

      if (paint_pos < min_paint) {
        // Put notes on the front
        int count = math::max(0, min_paint - paint_pos);
        for (int32_t i = 0; i < count; i++) {
          double time_pos = initial_time_pos + (double)new_length * (double)(min_paint - i - 1);
          if (time_pos >= 0.0) {
            painted_notes.emplace_at(
                0,
                MidiNote{
            .min_time = time_pos,
            .max_time = time_pos + (double)new_length,
            .key = key,
                  .flags = MidiNoteFlags::Modified,
                  .velocity = new_velocity / 127.0f,
                });
          }
        }
        min_paint = paint_pos;
      } else if (paint_pos > max_paint) {
        int32_t count = math::max(0, paint_pos - max_paint);
        // Put notes on the back
        for (int32_t i = 0; i < count; i++) {
          double time_pos = initial_time_pos + (double)new_length * (double)(i + max_paint + 1);
          painted_notes.push_back(MidiNote{
            .min_time = time_pos,
            .max_time = time_pos + (double)new_length,
            .key = key,
            .flags = MidiNoteFlags::Modified,
            .velocity = new_velocity / 127.0f,
          });
        }
        max_paint = paint_pos;
    }

    // Draw painted notes
    if (redraw) {
      for (const auto& note : painted_notes) {
        float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * clip_scale);
        float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * clip_scale);
        if (max_pos_x < cursor_pos.x)
          continue;
        if (min_pos_x > end_x)
          break;
        draw_note.operator()<false>(min_pos_x, max_pos_x, note.velocity, 0, note.key);
      }
    }
  } else if (edit_command == PianoRollTool::Move) {
    if (redraw) {
      const MidiNoteBuffer& seq = midi_asset->data.note_sequence;
      for (uint32_t id : fg_notes) {
        const MidiNote& note = seq[id];
        double min_time = note.min_time + relative_pos;
        double max_time = note.max_time + relative_pos;
        uint16_t key = (uint16_t)((int32_t)note.key + relative_key_pos);
        float min_pos_x = (float)math::round(scroll_offset_x + min_time * clip_scale);
        float max_pos_x = (float)math::round(scroll_offset_x + max_time * clip_scale);
        if (max_pos_x < cursor_pos.x)
          continue;
        if (min_pos_x > end_x)
          break;
        draw_note.operator()<false>(min_pos_x, max_pos_x, note.velocity, 0, key, note.flags);
      }
    }
  }

  // Display selection rectangle
  if (selecting_notes) {
    static const ImU32 selection_range_fill = Color(28, 150, 237, 72).to_uint32();
    static const ImU32 selection_range_border = Color(28, 150, 237, 255).to_uint32();
    float a_x = (float)math::round(scroll_offset_x + sel_start_pos * clip_scale);
    float b_x = (float)math::round(scroll_offset_x + sel_end_pos * clip_scale);
    float a_y = (float)(131 - sel_first_key) * note_height_in_pixel;
    float b_y = (float)(131 - sel_last_key + 1) * note_height_in_pixel;
    im_draw_rect_filled(layer2_dl, a_x, a_y + cursor_pos.y, b_x, b_y + cursor_pos.y, selection_range_fill);
    im_draw_rect(layer2_dl, a_x, a_y + cursor_pos.y, b_x, b_y + cursor_pos.y, selection_range_border);
  }

  if (hovered_note_id && false) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    Log::debug("{}", hovered_note_id.value());
  }

  if (redraw) {
    layer2_dl->PopClipRect();
    layer2_dl->PopTextureID();
    layer1_dl->PopClipRect();
    layer1_dl->PopTextureID();

    ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();
    g_renderer->begin_render(piano_roll_fb, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    layer_draw_data.Clear();
    layer_draw_data.DisplayPos = view_min;
    layer_draw_data.DisplaySize = region_size;
    layer_draw_data.FramebufferScale.x = 1.0f;
    layer_draw_data.FramebufferScale.y = 1.0f;
    layer_draw_data.OwnerViewport = owner_viewport;
    layer_draw_data.AddDrawList(layer1_dl);
    layer_draw_data.AddDrawList(layer2_dl);
    g_renderer->render_imgui_draw_data(&layer_draw_data);

    g_renderer->end_render();
  }

  ImTextureID fb_tex_id = (ImTextureID)piano_roll_fb;
  const ImVec2 fb_image_pos(view_min.x, offset_y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddImage(fb_tex_id, fb_image_pos, fb_image_pos + region_size);

  if (is_piano_roll_hovered && holding_ctrl && mouse_wheel != 0.0f) {
    zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel * zoom_rate);
    force_redraw = true;
  }

  last_vscroll = vscroll;

  ImGui::PopClipRect();
}

void ClipEditorWindow::render_event_editor() {
  auto draw_list = ImGui::GetWindowDrawList();
  auto cursor_pos = ImGui::GetCursorScreenPos() + ImVec2(vsplitter_min_size, 0.0f);
  auto editor_event_region = ImGui::GetContentRegionAvail();
  double view_scale = calc_view_scale();
  double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
  double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
  double pixel_scale = 1.0 / view_scale;
  float end_x = cursor_pos.x + timeline_width;
  float end_y = cursor_pos.y + editor_event_region.y;

  if (current_clip && current_clip->is_midi()) {
    for (auto note_data = current_clip->midi.asset; auto& note : note_data->data.note_sequence) {
      float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * pixel_scale);
      if (min_pos_x < cursor_pos.x)
        continue;
      if (min_pos_x > end_x)
        break;
      float min_pos_y = cursor_pos.y + (1.0f - note.velocity) * editor_event_region.y;
      ImVec2 min_pos = ImVec2(min_pos_x, min_pos_y);
      draw_list->AddLine(min_pos, ImVec2(min_pos_x, end_y), note_color);
      draw_list->AddRectFilled(min_pos - ImVec2(2.0f, 2.0f), min_pos + ImVec2(3.0f, 3.0f), note_color);
    }
  }
}

void ClipEditorWindow::draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& size, uint32_t oct) {
  ImU32 black_note = ImGui::GetColorU32(ImGuiCol_FrameBg);
  ImU32 white_note = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 separator = ImGui::GetColorU32(ImGuiCol_Separator);
  ImVec2 half_size = size * ImVec2(0.5f, 1.0f);
  ImVec2 note_pos = pos + ImVec2(half_size.x, 0.0f);
  float half_font_size = font->FontSize * 0.5f;
  uint32_t note_id = 11;

  for (int i = 0; i < 13; i++) {
    if (i == 7) {
      continue;
    }

    ImU32 bg_col;
    ImU32 text_col;
    if (i == 12) {
      bg_col = 0xFFAFAFAF;
      text_col = black_note;
    } else if (i % 2) {
      bg_col = black_note;
      text_col = white_note;
    } else {
      bg_col = 0xFFEFEFEF;
      text_col = black_note;
    }

    draw_list->AddRectFilled(note_pos, note_pos + half_size - ImVec2(0.0f, 1.0f), bg_col);

    if (size.y > 13.0f) {
      char note_name[5]{};
      float pos_y = size.y * 0.5f - half_font_size;
      const char* scale = note_scale[note_id];
      fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, oct);
      draw_list->AddText(note_pos + ImVec2(4.0f, pos_y), text_col, note_name);
    }

    note_pos.y += half_size.y;
    note_id--;
  }

  char note_name[5]{};
  fmt::format_to_n(note_name, sizeof(note_name), "C{}", oct);
  im_draw_simple_text(draw_list, note_name, ImVec2(pos.x + 4.0f, note_pos.y - font->FontSize - 4.0f), 0xFFFFFFFF);
  im_draw_hline(draw_list, note_pos.y - 1.0f, pos.x, pos.x + half_size.x, separator);
  pos.y = note_pos.y;
}

void ClipEditorWindow::select_or_deselect_all_notes(bool should_select) {
  MidiSelectOrDeselectNotesCmd* cmd = new MidiSelectOrDeselectNotesCmd();
  cmd->track_id = current_track_id.value();
  cmd->clip_id = current_clip_id.value();
  cmd->should_select = should_select;
  g_cmd_manager.execute("Clip editor: Select/deselect note", cmd);
}

void ClipEditorWindow::query_selected_range() {
}

void ClipEditorWindow::zoom_vertically(float mouse_pos_y, float height, float mouse_wheel) {
  float min_scroll_pos_normalized = vscroll / height;
  new_note_height = math::max(note_height + mouse_wheel, 5.0f);
  last_scroll_pos_y_normalized = min_scroll_pos_normalized;
}

ClipEditorWindow g_clip_editor;
}  // namespace wb