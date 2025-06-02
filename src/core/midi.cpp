#include "midi.h"


namespace wb {

static const char* note_scale[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

const char* get_midi_note_scale(int16_t note_number) {
  return note_scale[note_number % 12];
}

int get_midi_note_octave(int16_t note_number) {
  return note_number / 12;
}

}  // namespace wb