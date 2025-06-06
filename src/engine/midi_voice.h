#pragma once

#include <limits>

#include "core/common.h"
#include "core/list.h"
#include "core/vector.h"

namespace wb {

struct MidiVoice : public InplaceList<MidiVoice> {
  double max_time;
  float velocity;
  uint16_t channel;
  int16_t key;
};

struct MidiVoiceState {
  static constexpr uint32_t max_voices = sizeof(uint64_t) * 8;
  Vector<MidiVoice> voices;
  InplaceList<MidiVoice> allocated_voices;
  InplaceList<MidiVoice> free_voices;
  uint64_t voice_mask{};
  uint32_t used_voices{};
  uint32_t max_used_voices{};

  MidiVoiceState();
  bool add_voice(MidiVoice&& voice);
  MidiVoice* release_voice(double timeout);
  void release_all();
  inline bool has_voice() const {
    return used_voices != 0;
  }
};

}  // namespace wb
