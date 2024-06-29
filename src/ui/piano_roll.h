#pragma once

#include "core/midi.h"

namespace wb {
struct GuiPianoRoll {
    bool open = true;
    Vector<MidiNote> midi_note;

    void open_midi_file();
    void render();
};

extern GuiPianoRoll g_piano_roll;

} // namespace wb