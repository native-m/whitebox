#pragma once

#include <array>

#include "common.h"
#include "vector.h"

#define WB_INVALID_NOTE_METADATA_ID 0xFFFFFFFF
#define WB_INVALID_NOTE_ID          0xFFFFFFFF

namespace wb {

using NoteID = uint32_t;
using NoteSequenceID = uint32_t;

struct MidiNoteFlags {
  enum : uint16_t {
    Muted = 1 << 0,
    Modified = 1 << 13,
    Selected = 1 << 14,
    Deleted = 1 << 15,
    PrivateFlags = Modified | Selected | Deleted,
  };
};

// WARNING!!
// If this structure has been changed, the project writer needs to be changed too!
struct alignas(8) MidiNote {
  double min_time;
  double max_time;
  uint32_t meta_id;
  int16_t key;
  uint16_t flags;
  float velocity;
};
using MidiNoteBuffer = Vector<MidiNote>;

// Store additional note data
struct MidiNoteMetadata {
  uint32_t next_free_id;  // Next free note ID
  uint32_t note_id;        // The owner of this metadata
};
using MidiNoteMetadataPool = Vector<MidiNoteMetadata>;

struct MidiNoteState {
  uint64_t last_tick;
  float velocity;
  bool on;
};

struct MidiEditResult {
  Vector<uint32_t> modified_notes;
  Vector<MidiNote> deleted_notes;
};

using NoteCallback = void (*)(void* userdata, uint32_t id, const MidiNote& note);

struct MidiData {
  static constexpr uint16_t max_keys = 132;
  static constexpr uint32_t max_channels = 16;
  double max_length = 0.0;
  MidiNoteMetadataPool note_metadata_pool;
  MidiNoteBuffer note_sequence;
  // std::array<MidiNoteBuffer, max_channels> channels;
  uint32_t first_free_id = WB_INVALID_NOTE_METADATA_ID;
  uint32_t num_free_metadata = 0;
  uint32_t id_counter = 0;
  uint32_t num_selected = 0;

  // This is for GUI, we need to know which note is the lowest or highest note in the
  // midi buffer so we can calculate the Y-scale for each note.
  uint32_t min_note = 0;
  uint32_t max_note = 0;

  void create_metadata(MidiNote* notes, uint32_t count);
  void free_metadata(uint32_t id);
  NoteSequenceID find_note(double pos, uint16_t key, uint16_t channel);
  Vector<uint32_t> find_notes(double min_pos, double max_pos, uint16_t min_key, uint16_t max_key, uint16_t channel);
  void query_notes(
      double min_pos,
      double max_pos,
      uint16_t min_key,
      uint16_t max_key,
      uint16_t channel,
      void* cb_userdata,
      NoteCallback cb);
  Vector<uint32_t> update_channel(uint16_t channel);
};

const char* get_midi_note_scale(int16_t key);
int get_midi_note_octave(int16_t key);

}  // namespace wb