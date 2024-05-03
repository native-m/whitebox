#pragma once

#include "clip.h"
#include "core/common.h"

namespace wb {

enum class EventType {
    AudioStart,
    AudioEnd,
};

struct AudioEvent {
    double time;
    size_t sample_offset;
    uint32_t buffer_offset;
    Clip* clip;
};

struct MidiEvent {

};

struct Event {
    EventType type;

    union {
        AudioEvent audio;
        MidiEvent midi;
    };
};

} // namespace wb