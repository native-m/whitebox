#pragma once

#include "core/audio_buffer.h"
#include "core/bit_manipulation.h"
#include "core/vector.h"
#include "event.h"
// #include <random>

namespace wb {

struct TestSynthVoice {
    double phase;
    double frequency;
    float volume;
    float amp;
    float current_amp;
    uint16_t note_number;
};

struct TestSynth {
    static constexpr uint32_t max_voices = sizeof(uint64_t) * 8;
    const float env_speed = (float)(5.0 / 44100.0);
    Vector<TestSynthVoice> voices;
    // std::random_device rd {};
    uint64_t voice_mask = 0;

    TestSynth();
    void add_voice(const MidiEvent& voice);
    void remove_note(uint16_t note_number);
    void render(AudioBuffer<float>& output_buffer, double sample_rate, uint32_t buffer_offset, uint32_t length);
};

} // namespace wb