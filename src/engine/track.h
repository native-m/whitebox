#pragma once

#include "clip.h"
#include "core/memory.h"
#include "sample_table.h"
#include <imgui.h>
#include <string>
#include <vector>

namespace wb {

struct Track {
    std::string name;
    ImColor color {0.3f, 0.3f, 0.3f, 1.0f};
    float height = 60.0f;
    bool shown = true;

    float volume = 0.0f;

    Pool<Clip> clip_allocator;
    std::vector<Clip*> clips;

    Clip* add_audio_clip(const std::string& name, double min_time, double max_time,
                         double beat_duration, SampleAsset* asset);
    Clip* duplicate_clip(Clip* clip_to_duplicate, double min_time, double max_time);
    void move_clip(Clip* clip, double relative_pos);
    void resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration,
                     bool right_side);
    void delete_clip(uint32_t id);
    void update(Clip* clip);
};

} // namespace wb