#pragma once

#include "command2.h"
#include "core/vector.h"

namespace wb {

struct CmdMidi : public Command2 {
  Vector<uint32_t> modified_notes;
  uint32_t track_id;
  uint32_t clip_id;

  void restore();
};

struct CmdMidiAddNote : public CmdMidi {
  double start_time;
  double end_time;
  float velocity;
  int16_t note_key;
  uint16_t channel;

  bool execute() override final;
  void undo() override final;
};

}  // namespace wb