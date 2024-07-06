#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/midi.h"

namespace wb {

enum class EventType {
    None,
    StopSample,
    PlaySample,
};

enum class MidiMessageType {
    NoteOn,
    NoteOff,
    PolyPressure,
    ControlChange
};

struct MidiNoteOnEvent {
    uint16_t channel;
    uint16_t note_number;
    float tuning;
    float velocity;
    int32_t length;
    int32_t note_id;
};

struct MidiNoteOffEvent {
    uint16_t channel;
    uint16_t note_number;
    float velocity;
    int32_t note_id;
    float tuning;
};

struct MidiPolyPressureEvent {
    uint16_t channel;
    uint16_t note_number;
    float pressure;
    int32_t note_id;
};

struct MidiControlChangeEvent {
    uint16_t index;
    uint32_t data;
};

struct MidiEvent {
    MidiMessageType type;
    union {
        MidiNoteOnEvent note_on;
        MidiNoteOffEvent note_off;
        MidiPolyPressureEvent poly_pressure;
        MidiControlChangeEvent control_change;
    };
};

struct AudioEvent {
    EventType type;
    uint32_t buffer_offset;
    double time;
    size_t sample_offset;
    Sample* sample;
};

} // namespace wb