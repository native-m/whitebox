#pragma once

#include "core/midi.h"
#include "timeline_base.h"

namespace wb {
struct GuiPianoRoll : public TimelineBase {
  static constexpr float note_count = 132.0f;
  static constexpr float note_count_per_oct = 12.0f;
  static constexpr float max_oct_count = note_count / note_count_per_oct;
  static constexpr float note_height = 18.0f;
  static constexpr float note_height_padded = note_height + 1.0f;

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
  bool scrolling = false;
  bool force_redraw = false;

  GuiPianoRoll();
  void open_midi_file();
  void render();
  void render_editor();
  void render_event_editor();
  void draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& note_size, uint32_t oct);
};

extern GuiPianoRoll g_piano_roll;

}  // namespace wb