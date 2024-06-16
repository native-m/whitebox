#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/midi.h"

namespace wb {

enum class EventType {
    None,
    StopSample,
    PlaySample,
    StopMidi,
    PlayMidi,
};

struct AudioEvent {
    size_t sample_offset;
    Sample* sample;
};

struct MidiEvent {
    
};

struct Event {
    EventType type;
    uint32_t buffer_offset;
    double time;

    union {
        AudioEvent audio;
        MidiEvent midi;
    };
};

} // namespace wb