#pragma once

#include <array>

#include "common.h"
#include "math.h"
#include "vector.h"

namespace wb {

struct MidiNoteFlags {
  enum : uint16_t {
    Deactivated = 1 << 0,
    Modified = 1 << 1,
  };
};

// WARNING!!
// If this structure has been changed, the project writer needs to be changed too!
struct alignas(8) MidiNote {
  double min_time;
  double max_time;
  uint32_t id;
  uint16_t key;
  uint16_t flags;
  float velocity;
};
using MidiNoteBuffer = Vector<MidiNote>;

struct MidiNoteState {
  uint64_t last_tick;
  float velocity;
  bool on;
};

struct MidiData {
  static constexpr uint16_t max_notes = 132;
  static constexpr uint32_t max_channels = 16;
  double max_length = 0.0;
  std::array<MidiNoteBuffer, max_channels> channels;
  uint32_t channel_count = 0;

  // This is for GUI, we need to know which note is the lowest or highest note in the
  // midi buffer so we can calculate the Y-scale for each note.
  uint32_t min_note = 0;
  uint32_t max_note = 0;

  inline void add_channel(MidiNoteBuffer&& buffer) {
    channels[0] = std::move(buffer);
    channel_count++;
  }

  Vector<uint32_t> update_channel(uint16_t channel);
};

const char* get_midi_note_scale(uint16_t key);
int get_midi_note_octave(uint16_t key);

}  // namespace wb