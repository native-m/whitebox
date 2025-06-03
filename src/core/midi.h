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

    /// Private flags, should be discarded after changing the note data
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

struct MidiEditResult {
  Vector<uint32_t> modified_notes;
  Vector<MidiNote> deleted_notes;
};

const char* get_midi_note_scale(int16_t key);
int get_midi_note_octave(int16_t key);

}  // namespace wb