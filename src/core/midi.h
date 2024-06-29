#pragma once

#include "common.h"
#include "vector.h"
#include <array>
#include <filesystem>

namespace wb {
struct MidiNote {
    double min_time;
    double max_time;
    uint32_t id;
    uint16_t channel;
    uint16_t note_number;
    float velocity;
};

struct MidiNoteState {
    bool on;
    uint64_t last_tick;
    float velocity;
};

Vector<MidiNote> load_notes_from_smf0(const std::filesystem::path& path);
} // namespace wb