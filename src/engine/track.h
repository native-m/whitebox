#pragma once

#include "audio_param.h"
#include "clip.h"
#include "core/audio_buffer.h"
#include "core/bit_manipulation.h"
#include "core/memory.h"
#include "core/vector.h"
#include "event.h"
#include "event_list.h"
#include "param_changes.h"
#include "vu_meter.h"
#include <array>
#include <imgui.h>
#include <numbers>
#include <random>
#include <unordered_set>

namespace wb {

enum TrackParameter {
    TrackParameter_Volume,
    TrackParameter_Pan,
    TrackParameter_Mute,
    TrackParameter_Max,
};

struct MidiVoice {
    double max_time;
    float velocity;
    uint16_t channel;
    uint16_t note_number;
};

struct MidiVoiceState {
    static constexpr uint32_t max_voices = sizeof(uint64_t) * 8;
    std::array<MidiVoice, max_voices> voices;
    uint64_t voice_mask;

    inline void add_voice(MidiVoice&& voice) {
        int free_voice = std::countr_one(~voice_mask) - 1; // find free voice
        voices[free_voice] = std::move(voice);
        voice_mask |= 1ull << free_voice;
    }
};

struct SynthVoice {
    double phase;
    double frequency;
    float volume;
    uint16_t note_number;
};

struct TestSynth {
    static constexpr uint32_t max_voices = sizeof(uint64_t) * 8;
    std::array<SynthVoice, max_voices> voices;
    std::random_device rd {};
    uint64_t voice_mask;

    inline void add_voice(const MidiEvent& voice) {
        int free_voice = std::countr_one(~voice_mask) - 1;
        std::uniform_real_distribution<double> dis(0.0, 2.0);
        voices[free_voice] = {
            .phase = dis(rd),
            .frequency = get_midi_frequency(voice.note_on.note_number),
            .volume = voice.note_on.velocity,
            .note_number = voice.note_on.note_number,
        };
        voice_mask |= 1ull << free_voice;
    }

    inline void remove_note(uint16_t note_number) {
        uint64_t active_voice_bits = voice_mask;
        while (active_voice_bits) {
            int active_voice = next_set_bits(active_voice_bits);
            SynthVoice& voice = voices[active_voice];
            if (voice.note_number == note_number)
                voice_mask &= ~(1ull << active_voice);
        }
    }

    inline void render(AudioBuffer<float>& output_buffer, double sample_rate,
                       uint32_t buffer_offset, uint32_t length) {
        if (!voice_mask || length == 0)
            return;

        uint32_t count = buffer_offset + length;
        for (uint32_t i = buffer_offset; i < count; i++) {
            float sample = 0.0f;
            uint64_t active_voice_bits = voice_mask;

            while (active_voice_bits) {
                int active_voice = next_set_bits(active_voice_bits);
                SynthVoice& voice = voices[active_voice];
                sample += (float)(voice.phase >= 1.0 ? 1.0f : -1.0f) * voice.volume * 0.5f;
                voice.phase += voice.frequency / sample_rate;
                if (voice.phase >= 2.0)
                    voice.phase -= 2.0;
            }

            for (uint32_t c = 0; c < output_buffer.n_channels; c++) {
                output_buffer.mix_sample(c, i, sample);
            }
        }
    }
};

struct TrackEventState {
    Clip* current_clip;
    Clip* next_clip;
    double last_start_clip_position;
    uint32_t midi_note_idx;
};

struct TrackParameterState {
    float volume_db; // UI-part only
    float volume;
    float pan;
    bool mute;
    bool solo;
};

struct Track {
    std::string name;
    ImColor color {0.3f, 0.3f, 0.3f, 1.0f};
    float height = 60.0f;
    bool shown = true;

    Pool<Clip> clip_allocator;
    Vector<Clip*> clips;
    std::unordered_set<uint32_t> deleted_clip_ids;

    TrackEventState event_state {};
    Vector<AudioEvent> audio_event_buffer;
    AudioEvent current_audio_event {};
    size_t samples_processed {};

    MidiVoiceState midi_voice_state {};
    MidiEventList midi_event_list;
    TestSynth test_synth {};

    LevelMeterColorMode level_meter_color {};
    VUMeter level_meter[2] {};

    TrackParameterState ui_parameter_state {}; // UI-side state
    TrackParameterState parameter_state {};    // Audio-side state
    ParamChanges param_changes;
    ConcurrentQueue<ParamChange>
        ui_param_changes; // This handles UI to audio thread parameter state transfer

    Track();
    Track(const std::string& name, const ImColor& color, float height, bool shown,
          const TrackParameterState& track_param);
    ~Track();

    void set_volume(float db);
    void set_pan(float pan);
    void set_mute(bool mute);

    /**
     * @brief Add audio clip into the track.
     *
     * @param name Name of the clip.
     * @param min_time Start time in beats.
     * @param max_time End time in beats.
     * @param clip_info Audio clip data.
     * @param beat_duration Duration of the beat in seconds.
     * @return A pointer to the new clip.
     */
    Clip* add_audio_clip(const std::string& name, double min_time, double max_time,
                         const AudioClip& clip_info, double beat_duration);

    /**
     * @brief Add midi clip into the track.
     *
     * @param name Name of the clip.
     * @param min_time Start time in beats.
     * @param max_time End time in beats.
     * @param clip_info Audio clip data.
     * @param beat_duration Duration of the beat in seconds.
     * @return A pointer to the new clip.
     */
    Clip* add_midi_clip(const std::string& name, double min_time, double max_time,
                        const MidiClip& clip_info, double beat_duration);

    /**
     * @brief Create a new clip from existing clip.
     *
     * @param clip_to_duplicate Clip to duplicate.
     * @param min_time Start time in beats.
     * @param max_time End time in beats.
     * @param beat_duration Duration of the beat in seconds.
     * @return A pointer to the new clip.
     */
    Clip* duplicate_clip(Clip* clip_to_duplicate, double min_time, double max_time,
                         double beat_duration);

    /**
     * @brief Move the clip relative to its position.
     *
     * @param clip Clip to move.
     * @param relative_pos Relative position measured in beats.
     * @param beat_duration Duration of the beat in seconds.
     */
    void move_clip(Clip* clip, double relative_pos, double beat_duration);

    /**
     * @brief Resize the length of the clip.
     *
     * @param clip Clip to resize.
     * @param relative_pos Relative position to the original position measured in beats.
     * @param min_length Minimum clip length measured in beats.
     * @param beat_duration Beat duration in seconds.
     * @param right_side Which side to resize (false: left side, true: right side).
     */
    void resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration,
                     bool right_side);

    /**
     * @brief Delete clip by ID. Deleting clip directly is not possible due to the reordering.
     *
     * @param id Clip ID.
     */
    void delete_clip(uint32_t id);

    /**
     * @brief Update clip ordering and possibly trim or delete overlapping clip.
     *
     * @param clip The updated clip
     * @param beat_duration Beat duration in seconds.
     */
    void update(Clip* clip, double beat_duration);

    /**
     * @brief Find next clip at a certain time.
     *
     * @param time_pos Search starting position in beats.
     * @param hint Hint Clip ID to speed up search.
     * @return A pointer to clip.
     */
    Clip* find_next_clip(double time_pos, uint32_t hint = WB_INVALID_CLIP_ID);

    /**
     * @brief Prepare track for playback.
     *
     * @param time_pos Starting point of the playback.
     */
    void prepare_play(double time_pos);

    /**
     * @brief Stop playback.
     */
    void stop();

    /**
     * @brief Process events.
     *
     * @param time_pos Position in beats.
     * @param beat_duration Duration of beat in seconds.
     * @param sample_rate Sample rate.
     * @param ppq Pulses per quarter note (PPQN).
     * @param inv_ppq Inverse of PPQN.
     */
    void process_event(uint32_t buffer_offset, double time_pos, double beat_duration,
                       double sample_rate, double ppq, double inv_ppq);

    void process_midi_event(Clip* clip, uint32_t buffer_offset, double time_pos,
                            double beat_duration, double ppq, double inv_ppq);

    /**
     * @brief Process audio block.
     *
     * @param output_buffer Position in beats.
     * @param sample_rate Sample rate.
     * @param playing Should play the track.
     */
    void process(AudioBuffer<float>& output_buffer, double sample_rate, bool playing);

    void render_sample(AudioBuffer<float>& output_buffer, uint32_t buffer_offset,
                       uint32_t num_samples, double sample_rate);
    void update_playback_state(AudioEvent& event);
    void stream_sample(AudioBuffer<float>& output_buffer, Sample* sample, uint32_t buffer_offset,
                       uint32_t num_samples, size_t sample_offset);

    void process_test_synth(AudioBuffer<float>& output_buffer, double sample_rate, bool playing);

    void flush_deleted_clips();
};

} // namespace wb