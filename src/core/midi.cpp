#include "midi.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include "bit_manipulation.h"
#include "core_math.h"
#include "debug.h"

namespace wb {

static const char* note_scale[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

void MidiData::create_metadata(MidiNote* notes, uint32_t count) {
  if (first_free_metadata_id != WB_INVALID_NOTE_METADATA_ID) {
    // Reuse metadata instances
    uint32_t num_reuse = math::min(count, num_free_metadata);
    uint32_t meta_id = first_free_metadata_id;
    for (uint32_t i = 0; i < num_reuse; i++) {
      if (meta_id == WB_INVALID_NOTE_METADATA_ID) {
        break;
      }
      notes->meta_id = meta_id;
      meta_id = note_metadata_pool[meta_id].next_free_id;
      notes++;
    }
    first_free_metadata_id = meta_id;
    num_free_metadata -= num_reuse;
    count -= num_reuse;
  }
  if (count > num_free_metadata) {
    // Create new metadata instances
    uint32_t first_id = id_counter;
    note_metadata_pool.resize(note_metadata_pool.size() + count);
    for (uint32_t i = 0; i < count; i++) {
      uint32_t meta_id = first_id + i;
      notes[i].meta_id = meta_id;
      note_metadata_pool[meta_id].next_free_id = WB_INVALID_NOTE_METADATA_ID;
    }
    id_counter += count;
  }
}

void MidiData::free_metadata(uint32_t id) {
  note_metadata_pool[id].next_free_id = first_free_metadata_id;
  first_free_metadata_id = id;
  num_free_metadata++;
}

NoteSequenceID MidiData::find_note_sequence_id(double pos, uint16_t key, uint16_t channel) {
  const MidiNoteBuffer& buffer = channels[channel];
  if (buffer.size() == 0)
    return (uint32_t)-1;

  uint32_t seq_id = (uint32_t)-1;
  for (uint32_t i = 0; i < buffer.size(); i++) {
    const MidiNote& note = buffer[i];
    if (pos >= note.min_time && pos < note.max_time && note.key == key) {
      seq_id = i; // TODO: Should probably return multiple notes!
      break;
    }
  }
  
  return seq_id;
}

Vector<uint32_t> MidiData::find_notes(double min_pos, double max_pos, uint16_t min_key, uint16_t max_key, uint16_t channel) {
  MidiNoteBuffer& buffer = channels[channel];
  Vector<uint32_t> notes;
  for (uint32_t i = 0; i < (uint32_t)buffer.size(); i++) {
    MidiNote& note = buffer[i];
    if (max_pos >= note.max_time) {
      break;
    }
    if (min_pos >= note.max_time && max_pos <= note.min_time) {
    }
  }
  return notes;
}

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
    note_metadata_pool[note.meta_id].data_id = i;
    if (has_bit(note.flags, MidiNoteFlags::Modified)) {
      note.flags &= ~MidiNoteFlags::Modified;
      modified_notes.push_back(note.meta_id);
    }
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