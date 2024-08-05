#pragma once

#include "core/midi.h"
#include "timeline_base.h"

namespace wb {
struct GuiPianoRoll : public TimelineBase {
    bool open = true;
    MidiData midi_note;

    ImVec2 content_size;
    float vscroll = 0.0f;
    float last_vscroll = 0.0f;
    float scroll_delta_y = 0.0f;
    bool scrolling = false;
    bool force_redraw = false;

    GuiPianoRoll();
    void open_midi_file();
    void render();

    void draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& note_size, uint32_t oct);
};

extern GuiPianoRoll g_piano_roll;

} // namespace wb