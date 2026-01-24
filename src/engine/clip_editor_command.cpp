#include "clip_editor_command.h"

#include "engine2.h"
#include "track.h"

namespace wb {

void CmdMidi::restore() {
  Track* track = Engine2::tracks[track_id];
  Clip* clip = track->clips[clip_id];
  MidiData* midi_data = clip->get_midi_data();
  MidiNoteBuffer& note_sequence = midi_data->note_sequence;
  MidiNoteBuffer new_sequence;
  new_sequence.reserve(note_sequence.size());

  for (uint32_t note_id = 0; auto& note : note_sequence) {
    bool skip = false;
    
    for (uint32_t id : modified_notes) {
      if (id == note_id) {
        skip = true;
        break;
      }
    }

    if (!skip) {
      new_sequence.push_back(note);
    }

    note_id++;
  }

  note_sequence = std::move(new_sequence);
  midi_data->update_channel(0);
}

//

bool CmdMidiAddNote::execute() {
  Track* track = Engine2::tracks[track_id];
  Clip* clip = track->clips[clip_id];
  assert(clip->type == ClipType::Midi && "Not a MIDI clip");

  MidiAsset2* asset = clip->midi.asset;
  MidiNoteBuffer& notes = asset->data.note_sequence;
  
  Engine2::begin_edit();

  MidiNote* note = notes.emplace_back_raw();
  new(note) MidiNote {
    .min_time = start_time,
    .max_time = end_time,
    .flags = MidiNoteFlags::Modified,
    .velocity = velocity,
  };

  modified_notes = asset->data.update_channel(channel);

  Engine2::end_edit();

  return true;
}

void CmdMidiAddNote::undo() {
  Engine2::begin_edit();
  restore();
  Engine2::end_edit();
}

}  // namespace wb