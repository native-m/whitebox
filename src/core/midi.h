#pragma once

#include "common.h"

namespace wb {

enum class MidiStatus {
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

struct MidiMessage {
    MidiStatus status;
    union {
        MidiNoteOnEvent note_on;
        MidiNoteOffEvent note_off;
        MidiPolyPressureEvent poly_pressure;
        MidiControlChangeEvent control_change;
    };
};

} // namespace wb