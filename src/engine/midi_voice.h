#pragma once

#include "core/common.h"
#include "core/list.h"
#include <array>
#include <limits>

namespace wb {

struct MidiVoice : public InplaceList<MidiVoice> {
    double max_time;
    float velocity;
    uint16_t channel;
    uint16_t note_number;
};

struct MidiVoiceState {
    static constexpr uint32_t max_voices = sizeof(uint64_t) * 8;
    std::array<MidiVoice, max_voices> voices;
    InplaceList<MidiVoice> allocated_voices;
    InplaceList<MidiVoice> free_voices;
    uint64_t voice_mask;
    uint32_t used_voices;
    uint32_t current_max_voices;
    double least_maximum_time = std::numeric_limits<double>::max();

    bool add_voice2(MidiVoice&& voice);
    MidiVoice* release_voice(double time_range);
    void release_all();
    inline bool has_voice() const { return used_voices != 0; }
};

} // namespace wb
