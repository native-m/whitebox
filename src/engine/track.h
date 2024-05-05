#pragma once

#include "clip.h"
#include "core/memory.h"
#include "core/queue.h"
#include "event.h"
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

    LocalQueue<Event, 64> event_queue;
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
     * @brief Process events.
     * 
     * @param time_pos Position in beats.
     * @param beat_duration Duration of beat in seconds.
     * @param sample_rate Sample rate.
     */
    void process_event(uint32_t buffer_offset, double time_pos, double beat_duration, double sample_rate);
};

} // namespace wb