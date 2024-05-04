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

struct Track {
    std::string name;
    ImColor color {0.3f, 0.3f, 0.3f, 1.0f};
    float height = 60.0f;
    float volume = 0.0f;
    bool shown = true;

    Pool<Clip> clip_allocator;
    std::vector<Clip*> clips;
    LocalQueue<Event, 64> events;

    Clip* add_audio_clip(const std::string& name, double min_time, double max_time,
                         const AudioClip& clip_info, double beat_duration);
    Clip* duplicate_clip(Clip* clip_to_duplicate, double min_time, double max_time,
                         double beat_duration);
    void move_clip(Clip* clip, double relative_pos, double beat_duration);
    void resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration,
                     bool right_side);
    void delete_clip(uint32_t id);
    void update(Clip* clip, double beat_duration);

    void process_event(double beat_pos, double beat_duration, double sample_rate);
};

} // namespace wb