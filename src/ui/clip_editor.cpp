#include "clip_editor.h"

#include <imgui.h>

#include "IconsMaterialSymbols.h"
#include "command_manager.h"
#include "controls.h"
#include "engine/engine.h"
#include "engine/track.h"
#include "font.h"
#include "gfx/renderer.h"
#include "grid.h"
#include "hotkeys.h"
#include "timeline.h"
#include "timeline_base.h"

namespace wb {

enum class PianoRollCmd {
  None,
  Select,
  Draw,
  Marker,
  Paint,
  Slice,

  // Implicit command
  Move,
  ResizeLeft,
  ResizeRight,
  Delete,
};

bool g_clip_editor_window_open = true;

static constexpr float note_count = 132.0f;
static constexpr float note_count_per_oct = 12.0f;
static constexpr float max_oct_count = note_count / note_count_per_oct;

static const char* note_str[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

static TimelineBase timeline_base{
  .vsplitter_size = 70.0f,
  .vsplitter_min_size = 70.0f,
};

static ImU32 separator_color;
static ImU32 border_color;
static ImU32 indicator_frame_color;
static ImU32 indicator_color;
static ImU32 note_color;
static ImU32 muted_note_color;
static ImU32 text_color;

static ImFont* font;
static ImDrawList* piano_roll_dl{};
static ImDrawList* layer1_dl{};
static ImDrawList* layer2_dl{};
static ImDrawData layer_draw_data;
static GPUTexture* piano_roll_fb{};

static Track* current_track;
static Clip* current_clip;
static std::optional<uint32_t> current_track_id;
static std::optional<uint32_t> current_clip_id;

static ImVec2 old_piano_roll_size;
static ImVec2 main_cursor_pos;
static float vscroll = 0.0f;
static float last_vscroll = 0.0f;
static float scroll_delta_y = 0.0f;
static float space_divider = 0.25f;
static float zoom_pos_y = 0.0f;
static float note_height = 18.0f;
static float note_height_in_pixel = note_height;
static float new_note_height = note_height;
static float note_editor_height = 0.0f;
static float last_scroll_pos_y_normalized = 0.0f;
static bool scrolling = false;
static bool zooming_vertically = false;
static bool holding_shift = false;
static bool holding_ctrl = false;
static bool holding_alt = false;
static bool selecting_notes = false;
static bool deleting_notes = false;
static bool append_selection = false;
static bool open_context_menu = false;
static bool force_redraw;

static double selection_start_pos = 0.0;
static double selection_end_pos = 0.0;
static uint32_t first_selected_key = 0;
static uint32_t last_selected_key = 0;

static bool triplet_grid = false;
static bool preview_note = true;
static int32_t grid_mode = 4;

static PianoRollCmd piano_roll_tool = PianoRollCmd::Draw;
static bool use_last_note = true;
static int note_channel = 1u;
static float note_velocity = 100.0f;
static float note_length = 1.0f;
static bool lock_pitch = true;

static PianoRollCmd edit_command{};
static double initial_time_pos = 0.0;
static double min_note_pos = 0.0;
static double max_relative_pos = 0.0;
static uint32_t edited_note_id = (uint32_t)WB_INVALID_NOTE_ID;
static int16_t min_note_key = 0;
static int16_t max_note_key = 0;
static int32_t initial_key = -1;
static int32_t hovered_key = -1;
static int32_t min_paint = 1;
static int32_t max_paint = INT32_MIN;
static std::optional<uint32_t> note_id_context_menu;
static Vector<MidiNote> painted_notes;
static Vector<uint32_t> fg_notes;

static void clip_editor_zoom_vertically(float mouse_pos_y, float height, float mouse_wheel) {
  float min_scroll_pos_normalized = vscroll / height;
  new_note_height = math::max(note_height + mouse_wheel, 5.0f);
  last_scroll_pos_y_normalized = min_scroll_pos_normalized;
}

static void clip_editor_delete_notes(bool selected) {
  MidiDeleteNoteCmd* cmd = new MidiDeleteNoteCmd();
  cmd->track_id = current_track_id.value();
  cmd->clip_id = current_clip_id.value();
  cmd->selected = selected;
  g_cmd_manager.execute("Clip editor: Delete notes", cmd);
  g_timeline.redraw_screen();
  deleting_notes = false;
  force_redraw = true;
  timeline_base.redraw = true;
}

static void clip_editor_prepare_move() {
  MidiData* midi_data = current_clip->get_midi_data();
  uint32_t num_selected = midi_data->num_selected;
  bool first = true;
  min_note_key = MidiData::max_keys;
  max_note_key = 0;
  for (const auto& note : midi_data->note_sequence) {
    if (contain_bit(note.flags, MidiNoteFlags::Selected)) {
      if (first) {
        min_note_pos = note.min_time;
        first = false;
      }
      min_note_key = math::min(min_note_key, note.key);
      max_note_key = math::max(max_note_key, note.key);
      num_selected--;
    }
    if (num_selected == 0) {
      break;
    }
  }
}

static void clip_editor_prepare_resize() {
}

static void clip_editor_select_or_deselect_all_notes(bool should_select) {
  MidiSelectOrDeselectNotesCmd* cmd = new MidiSelectOrDeselectNotesCmd();
  cmd->track_id = current_track_id.value();
  cmd->clip_id = current_clip_id.value();
  cmd->should_select = should_select;
  g_cmd_manager.execute("Clip editor: Select/deselect note", cmd);
}

static void clip_editor_process_hotkey() {
  if (hkey_pressed(Hotkey::PianoRollSelectTool)) {
    piano_roll_tool = PianoRollCmd::Select;
  } else if (hkey_pressed(Hotkey::PianoRollDrawTool)) {
    piano_roll_tool = PianoRollCmd::Draw;
  } else if (hkey_pressed(Hotkey::PianoRollMarkerTool)) {
    piano_roll_tool = PianoRollCmd::Marker;
  } else if (hkey_pressed(Hotkey::PianoRollPaintTool)) {
    piano_roll_tool = PianoRollCmd::Paint;
  } else if (hkey_pressed(Hotkey::PianoRollSliceTool)) {
    piano_roll_tool = PianoRollCmd::Slice;
  }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
    if (hkey_pressed(Hotkey::Delete)) {
      clip_editor_delete_notes(true);
    }
  }
}

static void clip_editor_render_toolbar() {
  const ImVec4 selected_tool_color =
      ImGui::ColorConvertU32ToFloat4(Color(ImGui::GetStyleColorVec4(ImGuiCol_Button)).brighten(0.15f).to_uint32());

  bool select_tool = piano_roll_tool == PianoRollCmd::Select;
  bool draw_tool = piano_roll_tool == PianoRollCmd::Draw;
  bool marker_tool = piano_roll_tool == PianoRollCmd::Marker;
  bool paint_tool = piano_roll_tool == PianoRollCmd::Paint;
  bool slice_tool = piano_roll_tool == PianoRollCmd::Slice;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
  ImGui::BeginChild(
      "##piano_roll_toolbar",
      ImVec2(),
      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY,
      ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar(1);

  set_current_font(FontType::Icon);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));

  if (controls::toggle_button(ICON_MS_ARROW_SELECTOR_TOOL "##pr_select", select_tool, selected_tool_color)) {
    piano_roll_tool = PianoRollCmd::Select;
  }
  controls::item_tooltip("Select tool");
  ImGui::SameLine(0.0f, 0.0f);

  if (controls::toggle_button(ICON_MS_STYLUS "##pr_draw", draw_tool, selected_tool_color)) {
    piano_roll_tool = PianoRollCmd::Draw;
  }
  controls::item_tooltip("Draw tool");
  ImGui::SameLine(0.0f, 0.0f);

  if (controls::toggle_button(ICON_MS_INK_HIGHLIGHTER_MOVE "##pr_marker", marker_tool, selected_tool_color)) {
    piano_roll_tool = PianoRollCmd::Marker;
  }
  controls::item_tooltip("Marker tool");
  ImGui::SameLine(0.0f, 0.0f);

  if (controls::toggle_button(ICON_MS_DRAW "##pr_paint", paint_tool, selected_tool_color)) {
    piano_roll_tool = PianoRollCmd::Paint;
  }
  controls::item_tooltip("Paint tool");
  ImGui::SameLine(0.0f, 0.0f);

  if (controls::toggle_button(ICON_MS_SURGICAL "##pr_slice", slice_tool, selected_tool_color)) {
    piano_roll_tool = PianoRollCmd::Slice;
  }
  controls::item_tooltip("Slice tool");
  ImGui::SameLine(0.0f);

  const char* preview_note_icon = preview_note ? (ICON_MS_VOLUME_UP "##pr_preview") : (ICON_MS_VOLUME_OFF "##pr_preview");
  controls::icon_toggle_button(preview_note_icon, &preview_note, ImColor(181, 230, 29));
  controls::item_tooltip("Preview note when editing");
  ImGui::SameLine(0.0f, 0.0f);

  controls::icon_toggle_button("\xef\x8b\x81##pr_last_note", &use_last_note, ImColor(0, 162, 232));
  controls::item_tooltip("Use last note properties");

  ImGui::PopStyleVar(2);
  set_current_font(FontType::Normal);

  if (any_of(piano_roll_tool, PianoRollCmd::Draw, PianoRollCmd::Marker, PianoRollCmd::Paint)) {
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 6.5f);
    ImGui::SameLine(0.0f);
    ImGui::PushItemWidth(80.0f);
    ImGui::DragInt("##note_ch", &note_channel, 0.25f, 1, 16, "Channel: %d", ImGuiSliderFlags_Vertical);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::DragFloat("##note_vel", &note_velocity, 1.0f, 0.0f, 127.0f, "Vel: %.1f", ImGuiSliderFlags_Vertical);
    ImGui::PopItemWidth();

    if (piano_roll_tool != PianoRollCmd::Marker) {
      ImGui::PushItemWidth(100.0f);
      ImGui::SameLine(0.0f, 4.0f);
      ImGui::DragFloat("##note_len", &note_length, 0.1f, 0.0000f, 32.0f, "Length: %.4f", ImGuiSliderFlags_Vertical);
      ImGui::PopItemWidth();
    }

    if (piano_roll_tool == PianoRollCmd::Paint) {
      ImGui::SameLine(0.0f);
      ImGui::Checkbox("Lock pitch", &lock_pitch);
    }

    ImGui::PopStyleVar();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  ImGui::EndChild();
  ImGui::PopStyleVar(1);

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  im_draw_hline(ImGui::GetWindowDrawList(), cursor_pos.y - 1.0f, cursor_pos.x, cursor_pos.x + width, border_color);
}

static void clip_editor_render_control_sidebar() {
  if (ImGui::BeginMenuBar()) {
    const char* track_name = current_track->name.empty() ? "<unnamed track>" : current_track->name.c_str();
    const char* clip_name = current_clip->name.empty() ? "<unnamed clip>" : current_clip->name.c_str();
    ImGui::Text("%s - %s", clip_name, track_name);
    ImGui::EndMenuBar();
  }

  ImGui::PushItemWidth(-FLT_MIN);
  if (grid_combo_box("##grid_mode", &grid_mode, &triplet_grid)) {
    timeline_base.redraw = true;
  }
  ImGui::PopItemWidth();

  ImGui::Separator();

  // ImGui::Checkbox("Looping", &current_clip->midi.loop);
  const char* items[] = { "One shot", "Reverse one shot", "Loop", "Reverse loop", "Bidirectional loop" };
  int mode = (int)current_clip->midi.mode;
  if (ImGui::Combo("Mode", &mode, items, IM_ARRAYSIZE(items))) {
    current_clip->midi.mode = (ClipMode)mode;
  }

  controls::musical_unit_drags("Length", &current_clip->midi.length);

  controls::with_command(
      &current_clip->midi.transpose,
      [](int16_t* value) {
        return controls::generic_drag(
            "Transpose",
            &current_clip->midi.transpose,
            0.5f,
            -48,
            48,
            math::in_range(current_clip->midi.transpose, (int16_t)-1, (int16_t)1) ? "%d semitone" : "%d semitones",
            ImGuiSliderFlags_Vertical);
      },
      [](int16_t old_value, int16_t new_value) {
        MidiClipParamChangeCmd* cmd = new MidiClipParamChangeCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->new_transpose = new_value;
        cmd->new_rate = current_clip->midi.rate;
        cmd->old_transpose = old_value;
        cmd->old_rate = current_clip->midi.rate;
        g_cmd_manager.execute("Clip editor: Clip parameter tweak (transpose)", cmd);
      });

  controls::with_command(
      &current_clip->midi.rate,
      [](int16_t* value) {
        static constexpr uint32_t drag_slider_flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Vertical;
        if (controls::generic_drag("Rate", value, 0.125f, 1, 4, "%dx", drag_slider_flags)) {
          timeline_base.redraw = true;
          g_timeline.redraw_screen();
          return true;
        }
        return false;
      },
      [](int16_t old_value, int16_t new_value) {
        MidiClipParamChangeCmd* cmd = new MidiClipParamChangeCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->new_transpose = current_clip->midi.transpose;
        cmd->new_rate = new_value;
        cmd->old_transpose = current_clip->midi.transpose;
        cmd->old_rate = old_value;
        g_cmd_manager.execute("Clip editor: Clip parameter tweak (rate)", cmd);
      });
}

static void draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& size, uint32_t oct) {
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
      const char* scale = note_str[note_id];
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

static void clip_editor_render_note_keys() {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(
      "PianoRollKeys",
      ImVec2(timeline_base.vsplitter_min_size, note_count * note_height_in_pixel),
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
      clip_editor_zoom_vertically(zoom_pos_y, note_count * note_height_in_pixel, (float)y * 0.1f);
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
  uint32_t oct_count = (uint32_t)std::ceil(note_editor_height / keys_height);
  int oct_scroll_offset = (uint32_t)(max_oct_count - std::floor(vscroll / keys_height)) - oct_count - 1;
  for (int i = oct_count; i >= 0; i--) {
    int oct_offset = i + oct_scroll_offset;
    if (oct_offset < 0)
      break;
    draw_piano_keys(piano_roll_dl, oct_pos, ImVec2(timeline_base.vsplitter_min_size, note_height_in_pixel), oct_offset);
  }
}

static void clip_editor_render_note_editor() {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 region_size = ImGui::GetContentRegionAvail();
  timeline_base.timeline_width = region_size.x;

  double view_scale = timeline_base.calc_view_scale();
  double inv_view_scale = 1.0 / view_scale;
  float max_height = note_count * note_height_in_pixel;
  float offset_y = vscroll + cursor_pos.y;
  ImVec2 view_min(cursor_pos.x, offset_y);
  ImVec2 view_max(cursor_pos.x + timeline_base.timeline_width, offset_y + region_size.y);
  ImGui::PushClipRect(view_min, view_max, true);

  const GridProperties grid_prop = get_grid_properties(grid_mode);
  double triplet_div = (grid_prop.max_division > 1.0 && triplet_grid) ? 1.5 : 1.0;
  timeline_base.beat_division = grid_prop.max_division == DBL_MAX
                                    ? calc_bar_division(inv_view_scale, grid_prop.gap_scale, triplet_grid) * 0.25
                                    : grid_prop.max_division * triplet_div * 0.25;

  ImGui::InvisibleButton(
      "PianoRollContent",
      ImVec2(region_size.x, max_height),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);

  // Resize piano roll framebuffer
  if (old_piano_roll_size.x != region_size.x || old_piano_roll_size.y != region_size.y) {
    static constexpr GPUTextureUsageFlags flags = GPUTextureUsage::Sampled | GPUTextureUsage::RenderTarget;
    int width = (int)math::max(region_size.x, 16.0f);
    int height = (int)math::max(region_size.y, 16.0f);
    if (piano_roll_fb)
      g_renderer->destroy_texture(piano_roll_fb);
    piano_roll_fb = g_renderer->create_texture(flags, GPUFormat::UnormB8G8R8A8, width, height, true, 0, 0, nullptr);
    assert(piano_roll_fb != nullptr);
    Log::debug("Piano roll framebuffer resized ({}x{})", (int)width, (int)height);
    old_piano_roll_size = region_size;
    timeline_base.redraw = timeline_base.redraw || true;
  }

  ImVec2 mouse_pos = ImGui::GetMousePos();
  float mouse_wheel = ImGui::GetIO().MouseWheel;
  float mouse_wheel_h = ImGui::GetIO().MouseWheelH;
  bool is_piano_roll_hovered = ImGui::IsItemHovered();
  bool is_active = ImGui::IsItemActive();
  bool is_activated = ImGui::IsItemActivated();
  bool left_mouse_clicked = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool left_mouse_down = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool middle_mouse_clicked = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  bool middle_mouse_down = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  bool right_mouse_clicked = is_activated && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  bool right_mouse_down = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Right);

  holding_shift = ImGui::IsKeyDown(ImGuiKey_ModShift);
  holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
  holding_alt = ImGui::IsKeyDown(ImGuiKey_ModAlt);

  if (is_piano_roll_hovered && mouse_wheel_h != 0.0f) {
    timeline_base.scroll_horizontal(mouse_wheel_h, timeline_base.song_length, -view_scale * 64.0);
  }

  // Assign scroll
  if (middle_mouse_clicked && middle_mouse_down && is_piano_roll_hovered)
    scrolling = true;

  // Do scroll
  if (scrolling) {
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
    timeline_base.scroll_horizontal(drag_delta.x, timeline_base.song_length, -view_scale);
    scroll_delta_y = drag_delta.y;
    if (scroll_delta_y != 0.0f)
      timeline_base.redraw = true;
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
  }

  // Release scroll
  if (!middle_mouse_down) {
    scrolling = false;
    scroll_delta_y = 0.0f;
  }

  if (edit_command != PianoRollCmd::None || selecting_notes) {
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
      timeline_base.scroll_horizontal(distance * speed, timeline_base.song_length, -view_scale);
    }
    if (mouse_pos.x > max_offset_x) {
      float distance = max_offset_x - mouse_pos.x;
      timeline_base.scroll_horizontal(distance * speed, timeline_base.song_length, -view_scale);
    }
    if (mouse_pos.y < min_offset_y) {
      float distance = min_offset_y - mouse_pos.y;
      scroll_delta_y = distance * speed;
    }
    if (mouse_pos.y > max_offset_y) {
      float distance = max_offset_y - mouse_pos.y;
      scroll_delta_y = distance * speed;
    }
    timeline_base.redraw = true;
  }

  ImVec2 area_size = ImVec2(timeline_base.timeline_width, region_size.y);
  ImU32 guidestrip_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.13f).to_uint32();
  ImU32 grid_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(0.55f).to_uint32();
  double min_hscroll = timeline_base.min_hscroll;
  double song_length = timeline_base.song_length;
  double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
  double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
  double note_scale = inv_view_scale;
  double hovered_position = 0.0;
  double hovered_position_grid = 0.0;

  hovered_key = MidiData::max_keys - (int32_t)((mouse_pos.y - cursor_pos.y) / note_height_in_pixel) - 1;
  if (is_piano_roll_hovered || is_active || edit_command != PianoRollCmd::None) {
    double beat_division = timeline_base.beat_division;
    hovered_position = ((double)(mouse_pos.x - cursor_pos.x) * view_scale + min_hscroll * song_length);
    hovered_position_grid = std::round(hovered_position * (double)beat_division) / (double)beat_division;
  }

  auto midi_asset = current_clip->midi.asset;
  bool notes_selected = midi_asset->data.num_selected > 0;

  if (holding_alt && right_mouse_clicked && edit_command == PianoRollCmd::None) {
    if (notes_selected) {
      clip_editor_select_or_deselect_all_notes(false);
    }
    deleting_notes = true;
    timeline_base.redraw = true;
  } else if (right_mouse_clicked) {
    open_context_menu = true;
  }

  if (!right_mouse_down && deleting_notes) {
    clip_editor_delete_notes(false);
  }

  // Start selection
  if (holding_ctrl && left_mouse_clicked && edit_command == PianoRollCmd::None) {
    selection_start_pos = hovered_position;
    first_selected_key = hovered_key;
    append_selection = holding_shift;
    selecting_notes = true;
  }

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
        uint32_t first_note = note_ids[0];
        MidiAppendNoteSelectionCmd* cmd = new MidiAppendNoteSelectionCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->select_or_deselect = true;
        cmd->selected_note_ids = std::move(note_ids);
        g_cmd_manager.execute("Clip editor: Append note selection", cmd);
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
    }

    selecting_notes = false;
    append_selection = false;
  }

  // Update selection bounds
  if (selecting_notes) {
    selection_end_pos = hovered_position;
    last_selected_key = hovered_key;
  }

  double min_move_pos = initial_time_pos - min_note_pos;
  int32_t min_key_move = initial_key - min_note_key;
  int32_t max_key_move = MidiData::max_keys - (max_note_key - initial_key) - 1;
  int32_t relative_key_pos = 0;
  double relative_pos = 0.0;
  double min_relative_pos = 0.0;
  double max_relative_pos = 0.0;

  if (edit_command == PianoRollCmd::Move) {
    relative_pos = math::max(hovered_position_grid, min_move_pos) - initial_time_pos;
    relative_key_pos = math::clamp(hovered_key, min_key_move, max_key_move) - initial_key;
    min_relative_pos = relative_pos;
    max_relative_pos = relative_pos;
  } else if (edit_command == PianoRollCmd::ResizeLeft) {
    min_relative_pos = math::max(hovered_position_grid, min_move_pos) - initial_time_pos;
  } else if (edit_command == PianoRollCmd::ResizeRight) {
    max_relative_pos = math::max(hovered_position_grid, min_move_pos) - initial_time_pos;
  }

  // Release action
  if (!left_mouse_down && edit_command != PianoRollCmd::None) {
    // NOTE(native-m):
    // Any edits will not be applied until the action is released.
    switch (edit_command) {
      case PianoRollCmd::Draw: {
        MidiAddNoteCmd* cmd = new MidiAddNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->min_time = hovered_position_grid;
        cmd->max_time = hovered_position_grid + note_length;
        cmd->velocity = note_velocity / 127.0f;
        cmd->note_key = hovered_key;
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Draw tool", cmd);
        break;
      }
      case PianoRollCmd::Marker: {
        MidiAddNoteCmd* cmd = new MidiAddNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->min_time = initial_time_pos;
        cmd->max_time = math::max(hovered_position_grid, initial_time_pos);
        cmd->velocity = note_velocity / 127.0f;
        cmd->note_key = initial_key;
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Marker tool", cmd);
        break;
      }
      case PianoRollCmd::Paint: {
        MidiPaintNotesCmd* cmd = new MidiPaintNotesCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->notes = std::move(painted_notes);
        cmd->channel = 0;
        g_cmd_manager.execute("Clip editor: Paint tool", cmd);
        break;
      }
      case PianoRollCmd::Move: {
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
    timeline_base.redraw = true;
    g_timeline.redraw_screen();
    edit_command = PianoRollCmd::None;
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

  if (timeline_base.redraw) {
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
    draw_musical_grid(layer1_dl, view_min, area_size, scroll_pos_x, inv_view_scale, grid_prop, 0.5f, triplet_grid);

    // Draw horizontal gridlines
    float key_pos_y = main_cursor_pos.y - std::fmod(vscroll, note_height_in_pixel);
    int num_keys = (int)math::min(math::round(note_editor_height / note_height_in_pixel), note_count);
    int key_index_offset = (int)(vscroll / note_height_in_pixel);
    ImVec2 key_pos = ImVec2(cursor_pos.x, key_pos_y - 1.0f);
    for (int i = 0; i <= num_keys; i++) {
      uint32_t index = i + key_index_offset;
      uint32_t note_semitone = index % 12;
      layer1_dl->AddLine(key_pos, key_pos + ImVec2(timeline_base.timeline_width, 0.0f), grid_color);

      if (note_semitone / 7) {
        note_semitone++;
      }

      if (note_semitone % 2 == 0) {
        layer1_dl->AddRectFilled(
            key_pos + ImVec2(0.0f, 1.0f),
            key_pos + ImVec2(timeline_base.timeline_width, note_height_in_pixel),
            guidestrip_color);
      }

      key_pos.y += note_height_in_pixel;
    }
  }

  auto font = ImGui::GetFont();
  float font_size = font->FontSize;
  float half_font_size = font_size * 0.5f;
  float half_note_size = note_height_in_pixel * 0.5f;
  float end_x = cursor_pos.x + timeline_base.timeline_width;
  float end_y = main_cursor_pos.y + note_editor_height;
  ImU32 handle_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
  bool start_command = !holding_ctrl && left_mouse_clicked && edit_command == PianoRollCmd::None;
  bool disable_command = selecting_notes;
  std::optional<uint32_t> hovered_note_id;

  auto draw_note =
      [&]<bool WithCommand>(
          float min_pos_x, float max_pos_x, float vel, uint32_t note_id, int16_t key, uint16_t flags = 0) -> PianoRollCmd {
    float pos_y = (float)(131 - key) * note_height_in_pixel;
    float min_pos_y = cursor_pos.y + pos_y;
    float max_pos_y = min_pos_y + note_height_in_pixel;

    ImVec2 a(min_pos_x + 0.5f, min_pos_y);
    ImVec2 b(max_pos_x + 0.5f, max_pos_y - 0.5f);
    if (a.y > end_y || b.y < main_cursor_pos.y)
      return PianoRollCmd::None;

    if (timeline_base.redraw) {
#ifdef WB_ENABLE_PIANO_ROLL_DEBUG_MENU
      if (display_note_id) {
        char str_id[16]{};
        fmt::format_to_n(str_id, sizeof(str_id), "{}", note_id);
        layer1_dl->AddText(ImVec2(a.x, a.y - font_size), 0xFFFFFFFF, str_id);
      }
#endif

      bool selected = contain_bit(flags, MidiNoteFlags::Selected);
      bool muted = contain_bit(flags, MidiNoteFlags::Muted);

      // Draw note rect
      layer1_dl->PathLineTo(a);
      layer1_dl->PathLineTo(ImVec2(b.x, a.y));
      layer1_dl->PathLineTo(b);
      layer1_dl->PathLineTo(ImVec2(a.x, b.y));
      layer1_dl->PathFillConvex(!muted ? note_color : muted_note_color);

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
        const char* scale = note_str[key % 12];
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

    PianoRollCmd command{};
    if constexpr (WithCommand) {
      if (holding_ctrl || selecting_notes) {
        return PianoRollCmd::None;
      }

      ImRect note_rect(min_pos_x, min_pos_y, max_pos_x, max_pos_y);
      if (is_piano_roll_hovered && note_rect.Contains(mouse_pos)) {
        static constexpr float handle_offset = 4.0f;
        ImRect left_handle(min_pos_x, min_pos_y, min_pos_x + handle_offset, max_pos_y);
        ImRect right_handle(max_pos_x - handle_offset, min_pos_y, max_pos_x, max_pos_y);
        if (deleting_notes) {
          command = PianoRollCmd::Delete;
        } else if (left_handle.Contains(mouse_pos)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          command = PianoRollCmd::ResizeLeft;
        } else if (right_handle.Contains(mouse_pos)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          command = PianoRollCmd::ResizeRight;
        } else if (piano_roll_tool != PianoRollCmd::Slice) {
          command = PianoRollCmd::Move;
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
  bool is_edit_command = any_of(edit_command, PianoRollCmd::Move, PianoRollCmd::ResizeLeft, PianoRollCmd::ResizeRight);

  if (is_piano_roll_hovered || is_active || timeline_base.redraw) {
    for (const auto& note : midi_asset->data.note_sequence) {
      uint16_t flags = note.flags;
      bool selected = contain_bit(flags, MidiNoteFlags::Selected);

      if (is_edit_command && (selected || note_id == edited_note_id)) {
        fg_notes.push_back(note_id);
        note_id++;
        continue;
      }

      if (contain_bit(flags, MidiNoteFlags::Deleted)) {
        note_id++;
        continue;
      }

      const float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * note_scale);
      const float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * note_scale);

      if (max_pos_x < cursor_pos.x) {
        note_id++;
        continue;
      }

      if (min_pos_x > end_x)
        break;

      // make it appear selected/deselected
      if (selecting_notes) {
        if (!append_selection) {
          flags &= ~MidiNoteFlags::Selected;
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
      auto cmd = draw_note.operator()<true>(min_pos_x, max_pos_x, note.velocity, note_id, note.key, flags);

      if (cmd == PianoRollCmd::Delete) {
        auto& note_delete = midi_asset->data.note_sequence[note_id];
        note_delete.flags |= MidiNoteFlags::Deleted;  // Mark this note deleted
        force_redraw = true;
      } else if (start_command && cmd != PianoRollCmd::None) {
        edit_command = cmd;
        initial_time_pos = hovered_position_grid;
        initial_key = hovered_key;
        edited_note_id = note_id;
        if (!notes_selected) {
          min_note_pos = cmd == PianoRollCmd::Move ? 0.0 : note.min_time;
          min_note_key = note.key;
          max_note_key = note.key;
        } else {
          if (cmd == PianoRollCmd::Move) {
            clip_editor_prepare_move();
          }
        }
        if (notes_selected && !selected) {
          clip_editor_select_or_deselect_all_notes(false);
          notes_selected = false;
        }
      }

      note_id++;
    }
  }

  // Register command
  if (!holding_ctrl && left_mouse_clicked && edit_command == PianoRollCmd::None) {
    if (notes_selected) {
      clip_editor_select_or_deselect_all_notes(false);
      notes_selected = false;
    }
    if (piano_roll_tool == PianoRollCmd::Slice) {
      MidiSliceNoteCmd* cmd = new MidiSliceNoteCmd();
      cmd->track_id = current_track_id.value();
      cmd->clip_id = current_clip_id.value();
      cmd->pos = hovered_position_grid;
      cmd->velocity = note_velocity / 127.0f;
      cmd->note_key = hovered_key;
      cmd->channel = 0;
      g_cmd_manager.execute("Clip editor: Slice tool", cmd);
      g_timeline.redraw_screen();
      force_redraw = true;
    } else if (piano_roll_tool == PianoRollCmd::Select) {
      selection_start_pos = hovered_position;
      first_selected_key = hovered_key;
      append_selection = holding_shift;
      selecting_notes = true;
    } else {
      edit_command = piano_roll_tool;
      initial_time_pos = hovered_position_grid;
      initial_key = hovered_key;
    }
  }

  // Handle commands
  if (edit_command == PianoRollCmd::Draw) {
    int16_t key = math::clamp(hovered_key, 0, (int32_t)MidiData::max_keys);
    double min_time = math::max(hovered_position_grid, 0.0);
    double max_time = min_time + note_length;
    float min_pos_x = (float)math::round(scroll_offset_x + min_time * note_scale);
    float max_pos_x = (float)math::round(scroll_offset_x + max_time * note_scale);
    draw_note.operator()<false>(min_pos_x, max_pos_x, note_velocity / 127.0f, 0, key);
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  } else if (edit_command == PianoRollCmd::Marker) {
    double min_time = math::max(initial_time_pos, 0.0);
    double max_time = math::max(hovered_position_grid, min_time);
    float min_pos_x = (float)math::round(scroll_offset_x + min_time * note_scale);
    float max_pos_x = (float)math::round(scroll_offset_x + max_time * note_scale);
    draw_note.operator()<false>(min_pos_x, max_pos_x, note_velocity / 127.0f, 0, initial_key);
  } else if (edit_command == PianoRollCmd::Paint) {
    int16_t key = lock_pitch ? initial_key : math::clamp(hovered_key, 0, (int32_t)MidiData::max_keys);
    double relative_pos = hovered_position_grid - initial_time_pos;
    int32_t paint_pos = (int32_t)std::floor(relative_pos / (double)note_length);

    if (paint_pos < min_paint) {
      // Put notes on the front
      int count = math::max(0, min_paint - paint_pos);
      for (int32_t i = 0; i < count; i++) {
        double time_pos = initial_time_pos + (double)note_length * (double)(min_paint - i - 1);
        if (time_pos >= 0.0) {
          painted_notes.emplace_at(
              0,
              MidiNote{
                .min_time = time_pos,
                .max_time = time_pos + (double)note_length,
                .key = key,
                .flags = MidiNoteFlags::Modified | MidiNoteFlags::Selected,
                .velocity = note_velocity / 127.0f,
              });
        }
      }
      min_paint = paint_pos;
    } else if (paint_pos > max_paint) {
      int32_t count = math::max(0, paint_pos - max_paint);
      // Put notes on the back
      for (int32_t i = 0; i < count; i++) {
        double time_pos = initial_time_pos + (double)note_length * (double)(i + max_paint + 1);
        painted_notes.push_back(MidiNote{
          .min_time = time_pos,
          .max_time = time_pos + (double)note_length,
          .key = key,
          .flags = MidiNoteFlags::Modified | MidiNoteFlags::Selected,
          .velocity = note_velocity / 127.0f,
        });
      }
      max_paint = paint_pos;
    }

    // Draw painted notes
    if (timeline_base.redraw) {
      for (const auto& note : painted_notes) {
        float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * note_scale);
        float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * note_scale);
        if (max_pos_x < cursor_pos.x)
          continue;
        if (min_pos_x > end_x)
          break;
        draw_note.operator()<false>(min_pos_x, max_pos_x, note.velocity, 0, note.key, note.flags);
      }
    }
  } else if (is_edit_command && timeline_base.redraw) {
    const MidiNoteBuffer& seq = midi_asset->data.note_sequence;
    for (uint32_t id : fg_notes) {
      const MidiNote& note = seq[id];
      double min_time = note.min_time + min_relative_pos;
      double max_time = note.max_time + max_relative_pos;
      int16_t key = (int16_t)((int32_t)note.key + relative_key_pos);
      float min_pos_x = (float)math::round(scroll_offset_x + min_time * note_scale);
      float max_pos_x = (float)math::round(scroll_offset_x + max_time * note_scale);
      if (max_pos_x < cursor_pos.x)
        continue;
      if (min_pos_x > end_x)
        break;
      draw_note.operator()<false>(min_pos_x, max_pos_x, note.velocity, 0, key, note.flags);
    }
  }

  // Display selection rectangle
  if (selecting_notes) {
    static const ImU32 selection_range_fill = Color(28, 150, 237, 72).to_uint32();
    static const ImU32 selection_range_border = Color(28, 150, 237, 255).to_uint32();
    float a_x = (float)math::round(scroll_offset_x + sel_start_pos * note_scale);
    float b_x = (float)math::round(scroll_offset_x + sel_end_pos * note_scale);
    float a_y = (float)(131 - sel_first_key) * note_height_in_pixel;
    float b_y = (float)(131 - sel_last_key + 1) * note_height_in_pixel;
    im_draw_rect_filled(layer2_dl, a_x, a_y + cursor_pos.y, b_x, b_y + cursor_pos.y, selection_range_fill);
    im_draw_rect(layer2_dl, a_x, a_y + cursor_pos.y, b_x, b_y + cursor_pos.y, selection_range_border);
  }

  if (timeline_base.redraw) {
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

  if (open_context_menu) {
    note_id_context_menu = hovered_note_id;
  }

  ImTextureID fb_tex_id = (ImTextureID)piano_roll_fb;
  const ImVec2 fb_image_pos(view_min.x, offset_y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddImage(fb_tex_id, fb_image_pos, fb_image_pos + region_size);

  if (g_engine.is_playing()) {
    const double clip_rate = (double)current_clip->midi.rate;
    const double playhead_offset = (timeline_base.playhead - current_clip->min_time) * clip_rate * inv_view_scale;
    const float playhead_pos = (float)math::round(view_min.x - scroll_pos_x + playhead_offset);
    if (math::in_range(playhead_pos, view_min.x, view_max.x)) {
      im_draw_vline(dl, playhead_pos, offset_y, offset_y + region_size.y, TimelineBase::playhead_color);
    }
  }

  if (is_piano_roll_hovered && holding_ctrl && mouse_wheel != 0.0f) {
    timeline_base.zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel * timeline_base.zoom_rate);
    force_redraw = true;
  }

  last_vscroll = vscroll;

  ImGui::PopClipRect();
}

static void clip_editor_render_event_editor() {
  if (ImGui::BeginChild("##piano_roll_event", ImVec2(), 0, ImGuiWindowFlags_NoBackground)) {
    auto draw_list = ImGui::GetWindowDrawList();
    auto cursor_pos = ImGui::GetCursorScreenPos() + ImVec2(timeline_base.vsplitter_min_size, 0.0f);
    auto editor_event_region = ImGui::GetContentRegionAvail();
    double view_scale = timeline_base.calc_view_scale();
    double scroll_pos_x = std::round((timeline_base.min_hscroll * timeline_base.song_length) / view_scale);
    double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
    double pixel_scale = 1.0 / view_scale;
    float end_x = cursor_pos.x + timeline_base.timeline_width;
    float end_y = cursor_pos.y + editor_event_region.y;

    if (current_clip && current_clip->is_midi()) {
      auto note_data = current_clip->midi.asset;
      for (auto& note : note_data->data.note_sequence) {
        float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * pixel_scale);
        if (min_pos_x < cursor_pos.x)
          continue;
        if (min_pos_x > end_x)
          break;
        if (contain_bit(note.flags, MidiNoteFlags::Deleted))
          continue;
        float min_pos_y = cursor_pos.y + (1.0f - note.velocity) * editor_event_region.y;
        ImVec2 min_pos = ImVec2(min_pos_x, min_pos_y);
        draw_list->AddLine(min_pos, ImVec2(min_pos_x, end_y), note_color);
        draw_list->AddRectFilled(min_pos - ImVec2(2.0f, 2.0f), min_pos + ImVec2(3.0f, 3.0f), note_color);
      }
    }
  }
  ImGui::EndChild();
}

static void clip_editor_render_piano_roll() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  timeline_base.render_horizontal_scrollbar();
  const double clip_rate = (double)current_clip->midi.rate;
  const double playhead_start = (g_engine.playhead_start - current_clip->min_time) * clip_rate;
  double new_time_pos = (timeline_base.playhead - current_clip->min_time) * clip_rate;
  if (timeline_base.render_time_ruler(&new_time_pos, playhead_start, selection_start_pos, selection_end_pos, false)) {
    g_engine.set_playhead_position(new_time_pos / clip_rate + current_clip->min_time);
  }
  ImGui::PopStyleVar();

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 child_content_size = ImGui::GetContentRegionAvail();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  im_draw_hline(
      draw_list,
      cursor_pos.y - 1.0f,
      cursor_pos.x,
      cursor_pos.x + child_content_size.x,
      ImGui::GetColorU32(ImGuiCol_Separator));

  bool note_height_changed = false;
  if (note_height != new_note_height) {
    note_height = new_note_height;
    note_height_changed = true;
  }

  main_cursor_pos = cursor_pos;
  note_height_in_pixel = math::round(note_height);
  note_editor_height = child_content_size.y * (1.0f - space_divider);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());

  if (note_height_changed) {
    ImGui::SetNextWindowScroll(ImVec2(0.0f, last_scroll_pos_y_normalized * note_height_in_pixel * note_count));
  }

  if (ImGui::BeginChild("NoteEditor", ImVec2(0.0f, note_editor_height), 0, ImGuiWindowFlags_NoBackground)) {
    const Color base_color = current_clip->color;
    indicator_frame_color = base_color.change_alpha(0.5f).darken(0.6f).to_uint32();
    indicator_color = base_color.darken(0.6f).to_uint32();
    note_color = base_color.brighten(0.75f).change_alpha(0.85f).to_uint32();
    muted_note_color = base_color.brighten(0.50f).desaturate(1.0f).to_uint32();
    text_color = base_color.darken(1.5f).to_uint32();
    piano_roll_dl = ImGui::GetWindowDrawList();
    vscroll = ImGui::GetScrollY();

    ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
    if (scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
      ImGui::SetScrollY(vscroll - scroll_delta_y);
      scroll_delta_y = 0.0f;
      timeline_base.redraw = true;
    }

    if ((last_vscroll - vscroll) != 0.0f)
      timeline_base.redraw = true;

    float separator_x = cursor_pos.x + timeline_base.vsplitter_min_size + 0.5f;
    im_draw_vline(piano_roll_dl, separator_x, cursor_pos.y, cursor_pos.y + note_editor_height, border_color, 2.0f);

    clip_editor_render_note_keys();
    clip_editor_render_note_editor();
  }
  ImGui::EndChild();

  if (controls::hsplitter(
          ImGui::GetID("##piano_roll_separator"),
          &note_editor_height,
          0.25f * child_content_size.y,
          0.0f,
          child_content_size.y)) {
    space_divider = 1.0 - (note_editor_height / child_content_size.y);
  }

  clip_editor_render_event_editor();

  ImGui::PopStyleVar();
}

static void clip_editor_render_context_menu() {
  if (ImGui::BeginPopup("##piano_roll_menu")) {
    MidiData* midi_data = current_clip->get_midi_data();
    if (midi_data->num_selected > 0) {
      ImGui::MenuItem("Invert selection");
      if (ImGui::MenuItem("Select All", "Ctrl+A")) {
        clip_editor_select_or_deselect_all_notes(true);
      }
      if (ImGui::MenuItem("Deselect All", "Ctrl+Shift+A")) {
        clip_editor_select_or_deselect_all_notes(false);
      }
      ImGui::MenuItem("Duplicate", "Ctrl+D");
      if (ImGui::MenuItem("Delete", "Del")) {
        clip_editor_delete_notes(true);
      }
      if (ImGui::MenuItem("Mute", "Ctrl+M")) {
        MidiMuteNoteCmd* cmd = new MidiMuteNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->should_mute = true;
        g_cmd_manager.execute("Clip editor: Mute note", cmd);
        force_redraw = true;
      }
      if (ImGui::MenuItem("Unmute", "Ctrl+Alt+M")) {
        MidiMuteNoteCmd* cmd = new MidiMuteNoteCmd();
        cmd->track_id = current_track_id.value();
        cmd->clip_id = current_clip_id.value();
        cmd->should_mute = false;
        g_cmd_manager.execute("Clip editor: Unmute note", cmd);
        force_redraw = true;
      }
    } else if (note_id_context_menu) {
      static float vel = 100.0f;

      ImGui::PushItemWidth(150.0f);
      if (ImGui::SliderFloat("Velocity", &vel, 0.0f, 127.0f, "%.1f")) {
      }
      ImGui::PopItemWidth();

      if (ImGui::IsItemDeactivated()) {
        Log::debug("Deactivated");
      }

      ImGui::MenuItem("Select All", "Ctrl+A");

      if (ImGui::MenuItem("Delete", "Del")) {
        uint32_t note_id = note_id_context_menu.value();
        midi_data->note_sequence[note_id].flags |= MidiNoteFlags::Deleted;
        clip_editor_delete_notes(false);
      }

      if (ImGui::MenuItem("Mute", "Ctrl+M")) {
      }
    } else {
      ImGui::MenuItem("Select All", "Ctrl+A");
    }
    ImGui::Separator();
    ImGui::MenuItem("Quantize");
    ImGui::EndPopup();
  }
}

void clip_editor_init() {
  g_cmd_manager.add_on_history_update_listener([&] { force_redraw = true; });
  layer1_dl = new ImDrawList(ImGui::GetDrawListSharedData());
  layer2_dl = new ImDrawList(ImGui::GetDrawListSharedData());
}

void clip_editor_shutdown() {
  delete layer1_dl;
  delete layer2_dl;
  if (piano_roll_fb)
    g_renderer->destroy_texture(piano_roll_fb);
}

void clip_editor_set_clip(uint32_t track_id, uint32_t clip_id) {
  current_track = g_engine.tracks[track_id];
  current_clip = current_track->clips[clip_id];
  current_track_id = track_id;
  current_clip_id = clip_id;
  force_redraw = true;
}

void clip_editor_unset_clip() {
  current_track = {};
  current_clip = {};
  current_track_id.reset();
  current_clip_id.reset();
  force_redraw = true;
}

Clip* clip_editor_get_clip() {
  return current_clip;
}

Track* clip_editor_get_track() {
  return current_track;
}

void render_clip_editor() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  if (!controls::begin_window("Clip Editor", &g_clip_editor_window_open)) {
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

  clip_editor_process_hotkey();

  border_color = ImGui::GetColorU32(ImGuiCol_Border);
  font = ImGui::GetFont();
  timeline_base.playhead = g_engine.playhead;

  if (ImGui::BeginChild(
          "##piano_roll_control", ImVec2(200.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_MenuBar)) {
    clip_editor_render_control_sidebar();
  }
  ImGui::EndChild();
  ImGui::SameLine(0.0f, 0.0f);

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  float height = ImGui::GetContentRegionAvail().y;
  im_draw_vline(ImGui::GetWindowDrawList(), cursor_pos.x, cursor_pos.y, cursor_pos.y + height, border_color);

  ImGui::SameLine(0.0f, 1.0f);
  if (ImGui::BeginChild("##piano_roll_control2", ImVec2(-FLT_MIN, 0.0f), 0, ImGuiWindowFlags_NoBackground)) {
    clip_editor_render_toolbar();
    clip_editor_render_piano_roll();
  }

  if (open_context_menu) {
    ImGui::OpenPopup("##piano_roll_menu");
    open_context_menu = false;
  }
  clip_editor_render_context_menu();

  ImGui::EndChild();

  controls::end_window();
}

}  // namespace wb