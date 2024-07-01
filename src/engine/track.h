#pragma once

#include "audio_param.h"
#include "clip.h"
#include "core/audio_buffer.h"
#include "core/memory.h"
#include "core/vector.h"
#include "vu_meter.h"
#include "event.h"
#include "param_changes.h"
#include <imgui.h>
#include <unordered_set>

namespace wb {

enum TrackParameter {
    TrackParameter_Volume,
    TrackParameter_Pan,
    TrackParameter_Mute,
    TrackParameter_Max,
};

struct TrackPlaybackState {
    Clip* current_clip;
    Clip* next_clip;
    double last_start_clip_position;
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

    Vector<Event> event_buffer;
    TrackPlaybackState playback_state {};
    Event last_event {};
    Event current_event {};
    size_t samples_processed {};

    LevelMeterColorMode level_meter_color {};
    VUMeter level_meter[2] {};

    TrackParameterState ui_parameter_state {}; // UI-side state
    TrackParameterState parameter_state {};    // Audio-side state
    ParamChanges param_changes;
    ConcurrentQueue<ParamChange> ui_param_changes; // This handles UI to audio thread parameter state transfer

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
    void update_playback_state(Event& event);
    void stream_sample(AudioBuffer<float>& output_buffer, Sample* sample, uint32_t buffer_offset,
                       uint32_t num_samples, size_t sample_offset);
    void flush_deleted_clips();
};

} // namespace wb