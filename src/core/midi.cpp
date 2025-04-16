#include "midi.h"

#include <midi-parser.h>

#include <fstream>
#include <limits>

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

bool load_notes_from_file(MidiData& result, const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return {};
  }

  size_t size = std::filesystem::file_size(path);
  if (size < 14) {
    return {};
  }

  char magic_code[4];
  file.read(magic_code, 4);
  if (std::memcmp(magic_code, "MThd", 4)) {
    return {};
  }

  file.seekg(0);

  Vector<MidiNote> notes;
  Vector<uint8_t> bytes(size);
  file.read((char*)bytes.data(), size);

  midi_parser parser{
    .state = MIDI_PARSER_INIT,
    .in = bytes.data(),
    .size = (int32_t)size,
  };

  Vector<MidiNoteState> note_state;
  double time_division = 0;
  double length = 0;
  uint64_t tick = 0;
  uint32_t min_note = std::numeric_limits<uint32_t>::max();
  uint32_t max_note = 0u;
  bool running = true;
  note_state.resize(255);
  uint32_t note_id = 0;

  while (running) {
    midi_parser_status status = midi_parse(&parser);
    switch (status) {
      case MIDI_PARSER_HEADER: time_division = 1.0 / (double)parser.header.time_division; break;
      case MIDI_PARSER_TRACK: tick = 0; break;
      case MIDI_PARSER_TRACK_MIDI: {
        tick += parser.vtime;
        switch (parser.midi.status) {
          case MIDI_STATUS_NOTE_ON: {
            MidiNoteState& state = note_state[parser.midi.param1];
            state.on = true;
            state.last_tick = tick;
            state.velocity = (float)parser.midi.param2 / 127.0f;
            break;
          }
          case MIDI_STATUS_NOTE_OFF: {
            MidiNoteState& state = note_state[parser.midi.param1];
            if (!state.on) {
              break;
            }
            double min_time = (double)state.last_tick * time_division;
            double max_time = (double)tick * time_division;
            notes.push_back(MidiNote{
              .min_time = min_time,
              .max_time = max_time,
              .id = note_id++,
              .key = parser.midi.param1,
              .velocity = state.velocity,
            });
            state.on = false;
            state.last_tick = tick;
            min_note = math::min((uint32_t)parser.midi.param1, min_note);
            max_note = math::max((uint32_t)parser.midi.param1, max_note);
            length = math::max(length, max_time);
            break;
          }
          default: break;
        }
        break;
      }
      case MIDI_PARSER_TRACK_META: tick += parser.vtime; break;
      case MIDI_PARSER_TRACK_SYSEX: tick += parser.vtime; break;
      case MIDI_PARSER_EOB: running = false; break;
      default: break;
    }
  }

  if (length < 0) {
    return {};
  }

  std::sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.min_time < b.min_time; });

  result.max_length = length;
  result.min_note = min_note;
  result.max_note = max_note;
  result.add_channel(std::move(notes));

  return true;
}

double get_midi_file_content_length(const std::filesystem::path& path) {
  return 0.0;
}

const char* get_midi_note_scale(uint16_t note_number) {
  return note_scale[note_number % 12];
}

int get_midi_note_octave(uint16_t note_number) {
  return note_number / 12;
}

}  // namespace wb