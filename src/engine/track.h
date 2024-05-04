#pragma once

#include "clip.h"
#include "core/memory.h"
#include "core/queue.h"
#include "event.h"
#include "sample_table.h"
#include <imgui.h>
#include <string>
#include <vector>

namespace wb {

struct TrackPlaybackState {
    uint32_t current_clip;
    uint32_t next_clip;
    double last_start_clip_position;
};

struct Track {
    std::string name;
    ImColor color {0.3f, 0.3f, 0.3f, 1.0f};
    float height = 60.0f;
    float volume = 0.0f;
    bool shown = true;

    Pool<Clip> clip_allocator;
    std::vector<Clip*> clips;
    LocalQueue<Event, 64> events;

    TrackPlaybackState playback_state;

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
     * @param relative_pos Relative position.
     * @param beat_duration Duration of the beat in seconds.
     */
    void move_clip(Clip* clip, double relative_pos, double beat_duration);
    void resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration,
                     bool right_side);
    void delete_clip(uint32_t id);
    void update(Clip* clip, double beat_duration);

    /**
     * @brief Find next clip at a certain time.
     * 
     * @param time_pos Search starting position in beats.
     * @param hint Hint Clip ID to speed up search.
     * @return Clip ID
     */
    uint32_t find_next_clip(double time_pos, uint32_t hint = WB_INVALID_CLIP_ID);

    void reset_playback_state(double time_pos);

    /**
     * @brief Process events.
     * 
     * @param time_pos Position in beats.
     * @param beat_duration Duration of beat in seconds.
     * @param sample_rate Sample rate.
     */
    void process_event(double time_pos, double beat_duration, double sample_rate);
};

} // namespace wb