#include "midi_voice.h"

#include <bit>

#include "core/core_math.h"

namespace wb {

MidiVoiceState::MidiVoiceState() {
  voices.resize(max_voices);
}

bool MidiVoiceState::add_voice(MidiVoice&& voice) {
  if (used_voices == max_voices)
    return false;

  // Find free voice. Allocate new one if there is any
  double max_time = voice.max_time;
  auto free_voice = static_cast<MidiVoice*>(free_voices.pop_next_item());
  if (free_voice != nullptr) {
    *free_voice = std::move(voice);
    allocated_voices.push_item(free_voice);
  } else {
    voices[max_used_voices] = std::move(voice);
    allocated_voices.push_item(&voices[max_used_voices]);
    if (max_used_voices != max_voices)
      max_used_voices++;
  }
  used_voices++;

  return true;
}

MidiVoice* MidiVoiceState::release_voice(double timeout) {
  auto active_voice = allocated_voices.next();
  if (active_voice == nullptr)
    return nullptr;

  auto shortest_voice = active_voice;
  while (active_voice != nullptr) {
    if (active_voice->max_time < shortest_voice->max_time && active_voice->max_time <= timeout)
      shortest_voice = active_voice;
    active_voice = active_voice->next();
  }

  if (shortest_voice->max_time > timeout)
    return nullptr;
  shortest_voice->remove_from_list();
  free_voices.push_item(shortest_voice);
  used_voices--;

  return shortest_voice;
}

void MidiVoiceState::release_all() {
  if (auto all_voices = allocated_voices.next()) {
    all_voices->pluck_from_list();
    free_voices.replace_next_item(all_voices);
  }
}

}  // namespace wb