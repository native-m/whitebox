#pragma once

#include "command2.h"

namespace wb {

struct CmdMidi : public Command2 { };

struct CmdMidiAddNote : public Command2 {
  double min_time;
  double max_time;
  float velocity;
  int16_t note_key;
  uint16_t channel;

  bool execute() override final;
  void undo() override final;
};

}  // namespace wb