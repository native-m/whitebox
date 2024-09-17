#pragma once

#include "common.h"
#include "math.h"
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
using MidiNoteBuffer = Vector<MidiNote>;

struct MidiNoteState {
    uint64_t last_tick;
    float velocity;
    bool on;
};

struct MidiData {
    static constexpr uint32_t max_channels = 16;
    double max_length = 0.0;
    std::array<MidiNoteBuffer, max_channels> channels;
    uint32_t channel_count = 0;

    // This is for GUI rendering, we need to know which note is the lowest or highest note in the
    // midi buffer so we can calculate the Y-scale for each note.
    uint32_t min_note = 0;
    uint32_t max_note = 0;

    inline void add_channel(MidiNoteBuffer&& buffer) {
        channels[0] = std::move(buffer);
        channel_count++;
    }
};

bool load_notes_from_file(MidiData& result, const std::filesystem::path& path);
const char* get_midi_note_scale(uint16_t note_number);
int get_midi_note_octave(uint16_t note_number);

static inline double get_midi_frequency(uint16_t note_number) {
    return 440.0f * std::exp2((double)(note_number - 69) / 12.0);
}
} // namespace wb