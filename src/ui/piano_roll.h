#pragma once

#include "core/midi.h"
#include "timeline_base.h"

namespace wb {

struct ClipEditorWindow : public TimelineBase {
  static constexpr float note_count = 132.0f;
  static constexpr float note_count_per_oct = 12.0f;
  static constexpr float max_oct_count = note_count / note_count_per_oct;

  ImDrawList* piano_roll_dl{};
  ImFont* font{};

  MidiData midi_note;
  ImVec2 content_size;
  ImVec2 main_cursor_pos;
  ImVec2 child_content_size;
  double ppq = 0.0;
  float vscroll = 0.0f;
  float last_vscroll = 0.0f;
  float scroll_delta_y = 0.0f;
  float space_divider = 0.25f;
  float content_height = 0.0f;
  float zoom_pos_y = 0.0f;
  float note_height = 18.0f;
  float note_height_in_pixel = note_height;
  float new_note_height = note_height;
  float last_scroll_pos_y_normalized = 0.0f;
  bool scrolling = false;
  bool force_redraw = false;
  bool zooming_vertically = false;

  ClipEditorWindow();
  void open_midi_file();
  void render();
  void render_note_keys();
  void render_note_editor();
  void render_event_editor();
  void draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& note_size, uint32_t oct);
  void zoom_vertically(float mouse_pos_y, float height, float mouse_wheel);
};

extern ClipEditorWindow g_piano_roll;

}  // namespace wb