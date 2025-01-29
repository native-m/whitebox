#include "midi_voice.h"
#include "core/core_math.h"
#include <bit>

namespace wb {
bool MidiVoiceState::add_voice2(MidiVoice&& voice) {
    if (used_voices == max_voices)
        return false;

    // Find free voice. Allocate new one if there is any
    double max_time = voice.max_time;
    auto free_voice = static_cast<MidiVoice*>(free_voices.pop_next_item());
    if (free_voice != nullptr) {
        *free_voice = std::move(voice);
        allocated_voices.push_item(free_voice);
    } else {
        voices[current_max_voices] = std::move(voice);
        allocated_voices.push_item(&voices[current_max_voices]);
        if (current_max_voices != max_voices)
            current_max_voices++;
    }
    least_maximum_time = math::max(least_maximum_time, max_time);
    used_voices++;

    return true;
}

MidiVoice* MidiVoiceState::release_voice(double time_range) {
    auto active_voice = allocated_voices.next();
    if (active_voice == nullptr)
        return nullptr;

    auto shortest_voice = active_voice;
    while (active_voice != nullptr) {
        if (active_voice->max_time < shortest_voice->max_time && active_voice->max_time <= time_range)
            shortest_voice = active_voice;
        active_voice = active_voice->next();
    }

    if (shortest_voice->max_time > time_range)
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

} // namespace wb