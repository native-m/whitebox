#include "midi_data.h"

#include <algorithm>

#include "core/bit_manipulation.h"
#include "core/core_math.h"

#define WB_ENABLE_NOTE_METADATA 0

namespace wb {

void MidiData::create_metadata(MidiNote* notes, uint32_t count) {
#if WB_ENABLE_NOTE_METADATA
  if (first_free_id != WB_INVALID_NOTE_METADATA_ID) {
    // Reuse metadata instances
    uint32_t num_reuse = math::min(count, num_free_metadata);
    uint32_t meta_id = first_free_id;
    for (uint32_t i = 0; i < num_reuse; i++) {
      if (meta_id == WB_INVALID_NOTE_METADATA_ID) {
        break;
      }
      notes->meta_id = meta_id;
      meta_id = note_metadata_pool[meta_id].next_free_id;
      notes++;
    }
    first_free_id = meta_id;
    num_free_metadata -= num_reuse;
    count -= num_reuse;
  }
  if (count > num_free_metadata) {
    // Create new metadata instances
    uint32_t first_id = id_counter;
    note_metadata_pool.expand_size(count);
    for (uint32_t i = 0; i < count; i++) {
      uint32_t meta_id = first_id + i;
      notes[i].meta_id = meta_id;
      note_metadata_pool[meta_id].next_free_id = WB_INVALID_NOTE_METADATA_ID;
    }
    id_counter += count;
  }
#endif
}

void MidiData::free_metadata(uint32_t id) {
#if WB_ENABLE_NOTE_METADATA
  note_metadata_pool[id].next_free_id = first_free_id;
  first_free_id = id;
  num_free_metadata++;
#endif
}

NoteSequenceID MidiData::find_note(double pos, uint16_t key, uint16_t channel) {
  if (note_sequence.size() == 0)
    return (uint32_t)-1;

  uint32_t note_id = (uint32_t)-1;
  for (uint32_t id = 0; const auto& note : note_sequence) {
    if (pos >= note.min_time && pos < note.max_time && note.key == key) {
      note_id = id;  // TODO: Should probably return multiple notes!
      break;
    }
    id++;
  }

  return note_id;
}

Vector<uint32_t> MidiData::find_notes(double min_pos, double max_pos, uint16_t min_key, uint16_t max_key, uint16_t channel) {
  Vector<uint32_t> notes;
  for (uint32_t note_id = 0; const auto& note : note_sequence) {
    if (note.max_time < min_pos || note.key < min_key || note.key > max_key) {
      note_id++;
      continue;
    }
    if (note.min_time > max_pos) {
      break;
    }
    notes.push_back(note_id);
    note_id++;
  }
  return notes;
}

void MidiData::query_notes(
    double min_pos,
    double max_pos,
    uint16_t min_key,
    uint16_t max_key,
    uint16_t channel,
    void* cb_userdata,
    NoteCallback cb) {
  for (uint32_t note_id = 0; const auto& note : note_sequence) {
    if (note.max_time < min_pos || note.key < min_key || note.key > max_key) {
      note_id++;
      continue;
    }
    if (note.min_time > max_pos) {
      break;
    }
    cb(cb_userdata, note_id, note);
    note_id++;
  }
}

Vector<uint32_t> MidiData::update_channel(uint16_t channel) {
  std::sort(note_sequence.begin(), note_sequence.end(), [](const MidiNote& a, const MidiNote& b) {
    if (a.min_time != b.min_time) {
      return a.min_time < b.min_time;
    }
    if (a.key != b.key) {
      return a.key < b.key;
    }
    return a.velocity < b.velocity;
  });

  Vector<uint32_t> modified_notes;
  int16_t new_min_note = max_keys;
  int16_t new_max_note = 0;
  double length = max_length;
  uint32_t selected_count = 0;

  for (uint32_t i = 0; i < (uint32_t)note_sequence.size(); i++) {
    MidiNote& note = note_sequence[i];
    length = math::max(length, note.max_time);
    new_min_note = math::min(new_min_note, note.key);
    new_max_note = math::max(new_max_note, note.key);
#if WB_ENABLE_NOTE_METADATA
    note_metadata_pool[note.meta_id].note_id = i;
#endif
    if (has_bit(note.flags, MidiNoteFlags::Modified)) {
      note.flags &= ~MidiNoteFlags::Modified;
      modified_notes.push_back(i);
    }
    if (has_bit(note.flags, MidiNoteFlags::Selected)) {
      selected_count++;
    }
  }

  max_length = length;
  min_note = new_min_note;
  max_note = new_max_note;
  num_selected = selected_count;

  return modified_notes;
}

}  // namespace wb