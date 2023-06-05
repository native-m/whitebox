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

    struct MIDIStatus
    {
        enum : uint8_t
        {
            CVNoteOff           = 0,
            CVNoteOn            = 1,
            CVPolyAftertouch    = 2,
            CVControlChange     = 3,
            CVProgramChange     = 4,
            CVChannelAftertouch = 5,
            CVPitchBend         = 6,
        };
    };

    // MIDI Channel Voice (CV) events
    inline constexpr uint8_t make_midi_cv_status(uint8_t type, uint8_t channel)
    {
        return 1 << 8 | type << 4 | channel & 4;
    }

    inline static double beat_to_seconds(double beat, double beat_duration)
    {
        return beat * beat_duration;
    }

    inline static double seconds_to_beat(double sec, double beat_duration)
    {
        return sec / beat_duration;
    }
}