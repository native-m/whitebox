#include "midi_file.h"

#include <midi-parser.h>

#include <fstream>
#include <algorithm>

#include "common.h"
#include "core_math.h"

namespace wb {

struct MidiNoteState {
  uint64_t last_tick;
  float velocity;
  bool on;
};

bool load_notes_from_file(MidiNoteBuffer& note_buffer, const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  size_t size = std::filesystem::file_size(path);
  if (size < 14) {
    return false;
  }

  char magic_code[4];
  file.read(magic_code, 4);
  if (std::memcmp(magic_code, "MThd", 4)) {
    return false;
  }

  file.seekg(0);

  Vector<uint8_t> bytes(size);
  file.read((char*)bytes.data(), size);

  midi_parser parser{
    .state = MIDI_PARSER_INIT,
    .in = bytes.data(),
    .size = (int32_t)size,
  };

  Vector<MidiNoteState> note_state;
  double time_division = 0;
  uint64_t tick = 0;
  uint32_t note_id = 0;
  bool running = true;
  note_state.resize(255);

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
            note_buffer.push_back(MidiNote{
              .min_time = min_time,
              .max_time = max_time,
              .key = parser.midi.param1,
              .velocity = state.velocity,
            });
            state.on = false;
            state.last_tick = tick;
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

  return true;
}

double get_midi_file_content_length(const std::filesystem::path& path) {
  return 0.0;
}

}  // namespace wb