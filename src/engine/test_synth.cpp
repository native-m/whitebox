#include "test_synth.h"

namespace wb {
TestSynth::TestSynth() {
    voices.resize(max_voices);
}

void TestSynth::add_voice(const MidiEvent& voice) {
    int free_voice = std::countr_one(~voice_mask) - 1;
    // std::uniform_real_distribution<double> dis(0.0, 2.0);
    voices[free_voice] = {
        .phase = 0.0,
        .frequency = get_midi_frequency(voice.note_on.note_number),
        .volume = voice.note_on.velocity,
        .amp = 1.0f,
        .note_number = voice.note_on.note_number,
    };
    voice_mask |= 1ull << free_voice;
}

void TestSynth::remove_note(uint16_t note_number) {
    uint64_t active_voice_bits = voice_mask;
    while (active_voice_bits) {
        int active_voice = next_set_bits(active_voice_bits);
        TestSynthVoice& voice = voices[active_voice];
        if (voice.note_number == note_number)
            voice_mask &= ~(1ull << active_voice);
    }
}

void TestSynth::render(AudioBuffer<float>& output_buffer, double sample_rate, uint32_t buffer_offset, uint32_t length) {
    if (!voice_mask || length == 0)
        return;

    uint32_t count = buffer_offset + length;
    for (uint32_t i = buffer_offset; i < count; i++) {
        float sample = 0.0f;
        uint64_t active_voice_bits = voice_mask;

        while (active_voice_bits) {
            int active_voice = next_set_bits(active_voice_bits);
            TestSynthVoice& voice = voices[active_voice];
            // double osc = std::sin(voice.phase * std::numbers::pi);
            double osc = voice.phase >= 1.0 ? 1.0f : -1.0f;
            sample += (float)osc * voice.amp * voice.volume * 0.5f;
            voice.phase += voice.frequency / sample_rate;
            voice.amp = std::max(voice.amp - env_speed, 0.0f);
            if (voice.phase >= 2.0)
                voice.phase -= 2.0;
        }

        for (uint32_t c = 0; c < output_buffer.n_channels; c++) {
            output_buffer.mix_sample(c, i, sample);
        }
    }
}
} // namespace wb