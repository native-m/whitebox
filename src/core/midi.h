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
    uint16_t note_number;
    float velocity;
};

struct MidiNoteState {
    bool on;
    uint64_t last_tick;
    float velocity;
};

using MidiNoteBuffer = Vector<MidiNote>;

Vector<MidiNote> load_notes_from_file(const std::filesystem::path& path);
} // namespace wb