#pragma once

#include "../core/midi.h"

namespace wb
{
    enum class AudioStatus : uint8_t
    {
        Start,
        Stop
    };

    struct AudioMessage
    {
        AudioStatus status;

        inline static constexpr AudioMessage start()
        {
            return AudioMessage{
                .status = AudioStatus::Start,
            };
        }

        inline static constexpr AudioMessage end()
        {
            return AudioMessage{
                .status = AudioStatus::Stop,
            };
        }
    };

    struct MIDIMessage
    {
        uint8_t status;
        uint8_t data0;
        uint8_t data1;

        inline static constexpr MIDIMessage note_off(uint8_t channel, uint8_t note_number, float velocity)
        {
            return MIDIMessage{
                .status = make_midi_cv_status(MIDIStatus::CVNoteOff, channel),
                .data0 = note_number,
                .data1 = (uint8_t)(velocity * 127.0f),
            };
        }

        inline static constexpr MIDIMessage note_on(uint8_t channel, uint8_t note_number, float velocity)
        {
            return MIDIMessage{
                .status = make_midi_cv_status(MIDIStatus::CVNoteOn, channel),
                .data0 = note_number,
                .data1 = (uint8_t)(velocity * 127.0f),
            };
        }

        inline static constexpr MIDIMessage poly_aftertouch(uint8_t channel, uint8_t note_number, float pressure)
        {
            return MIDIMessage{
                .status = make_midi_cv_status(MIDIStatus::CVPolyAftertouch, channel),
                .data0 = note_number,
                .data1 = (uint8_t)(pressure * 127.0f),
            };
        }

        inline static constexpr MIDIMessage control_change(uint8_t channel, uint8_t index, uint8_t data)
        {
            return MIDIMessage{
                .status = make_midi_cv_status(MIDIStatus::CVControlChange, channel),
                .data0 = index,
                .data1 = data,
            };
        }

        inline static constexpr MIDIMessage channel_aftertouch(uint8_t channel, float pressure)
        {
            return MIDIMessage{
                .status = make_midi_cv_status(MIDIStatus::CVChannelAftertouch, channel),
                .data0 = (uint8_t)(pressure * 127.0f),
                .data1 = 0,
            };
        }
    };

    struct TrackMessage
    {
        uint64_t sample_position;

        union
        {
            AudioMessage audio;
            MIDIMessage midi;
        };
    };
    
}