#pragma once

#include "core/midi.h"

namespace wb {

using NoteCallback = void (*)(void* userdata, uint32_t id, const MidiNote& note);

struct MidiData {
  static constexpr int16_t max_keys = 132;
  static constexpr uint32_t max_channels = 16;
  double max_length = 0.0;
  MidiNoteBuffer note_sequence;
  MidiNoteMetadataPool note_metadata_pool;
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

}  // namespace wb