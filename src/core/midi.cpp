#include "midi.h"

#include <fstream>
#include <limits>
#include <algorithm>

#include "bit_manipulation.h"
#include "core_math.h"
#include "debug.h"

namespace wb {

static const char* note_scale[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

Vector<uint32_t> MidiData::update_channel(uint16_t channel) {
  MidiNoteBuffer& buffer = channels[channel];
  std::sort(buffer.begin(), buffer.end(), [](const MidiNote& a, const MidiNote& b) { return a.min_time < b.min_time; });

  Vector<uint32_t> modified_notes;
  uint16_t new_min_note = max_notes;
  uint16_t new_max_note = 0;
  double length = max_length;

  for (uint32_t i = 0; i < (uint32_t)buffer.size(); i++) {
    MidiNote& note = buffer[i];
    length = math::max(length, note.max_time);
    new_min_note = math::min(new_min_note, note.key);
    new_max_note = math::max(new_max_note, note.key);
    if (has_bit(note.flags, MidiNoteFlags::Modified)) {
      note.flags &= ~MidiNoteFlags::Modified;
      modified_notes.push_back(i);
    }
    note.id = i;
  }

  max_length = length;
  min_note = new_min_note;
  max_note = new_max_note;
  channel_count = math::max(channel_count, (uint32_t)channel + 1);

  return modified_notes;
}

const char* get_midi_note_scale(uint16_t note_number) {
  return note_scale[note_number % 12];
}

int get_midi_note_octave(uint16_t note_number) {
  return note_number / 12;
}

}  // namespace wb