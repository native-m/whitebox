#pragma once

#include "../types.h"

namespace wb
{
    enum class MIDINotes
    {
        A,
        ASharp,
        B,
        C,
        CSharp,
        D,
        DSharp,
        E,
        F,
        FSharp,
        G,
        GSharp,
    };

    // MIDI Channel Voice (CV) events
    static constexpr uint8_t midi_cv_note_off           = 0;
    static constexpr uint8_t midi_cv_note_on            = 1;
    static constexpr uint8_t midi_cv_poly_aftertouch    = 2;
    static constexpr uint8_t midi_cv_control_change     = 3;
    static constexpr uint8_t midi_cv_program_change     = 4;
    static constexpr uint8_t midi_cv_channel_aftertouch = 5;
    static constexpr uint8_t midi_cv_pitch_bend         = 6;
    inline constexpr uint8_t make_midi_cv_status(uint8_t type, uint8_t channel) { return 1 << 8 | type << 4 | channel & 4; }

    struct MIDIMessage
    {
        double timestamp;
        uint8_t status;
        uint8_t data0;
        uint8_t data1;

        inline static constexpr MIDIMessage note_off(int channel, int note_number, float velocity, double timestamp = 0)
        {
            return MIDIMessage{
                .timestamp = timestamp,
                .status = make_midi_cv_status(midi_cv_note_off, channel),
                .data0 = (uint8_t)note_number,
                .data1 = (uint8_t)(velocity * 127.0f),
            };
        }

        inline static constexpr MIDIMessage note_on(int channel, int note_number, float velocity, double timestamp = 0)
        {
            return MIDIMessage{
                .timestamp = timestamp,
                .status = make_midi_cv_status(midi_cv_note_on, channel),
                .data0 = (uint8_t)note_number,
                .data1 = (uint8_t)(velocity * 127.0f),
            };
        }

        inline static constexpr MIDIMessage poly_aftertouch(int channel, int note_number, float pressure, double timestamp = 0)
        {
            return MIDIMessage{
                .timestamp = timestamp,
                .status = make_midi_cv_status(midi_cv_poly_aftertouch, channel),
                .data0 = (uint8_t)note_number,
                .data1 = (uint8_t)(pressure * 127.0f),
            };
        }

        inline static constexpr MIDIMessage control_change(int channel, int note_number, int value, double timestamp = 0)
        {
            return MIDIMessage{
                .timestamp = timestamp,
                .status = make_midi_cv_status(midi_cv_poly_aftertouch, channel),
                .data0 = (uint8_t)note_number,
                .data1 = (uint8_t)(value * 127.0f),
            };
        }

        inline static constexpr MIDIMessage channel_aftertouch(int channel, int note_number, float pressure, double timestamp = 0)
        {
            return MIDIMessage{
                .timestamp = timestamp,
                .status = make_midi_cv_status(midi_cv_channel_aftertouch, channel),
                .data0 = (uint8_t)note_number,
                .data1 = (uint8_t)(pressure * 127.0f),
            };
        }
    };

    inline static double beat_to_seconds(double beat, double beat_duration)
    {
        return beat * beat_duration;
    }
}