#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/core_math.h"
#include "etypes.h"

namespace wb {
static inline ClipMoveResult calc_move_clip(Clip* clip, double relative_pos) {
    const double new_pos = math::max(clip->min_time + relative_pos, 0.0);
    return {
        new_pos,
        new_pos + (clip->max_time - clip->min_time),
    };
}

static inline ClipResizeResult calc_resize_clip(Clip* clip, double relative_pos, double min_length,
                                                double beat_duration, bool is_min) {
    if (!is_min) {
        double new_max = math::max(clip->max_time + relative_pos, 0.0);
        if (new_max <= clip->min_time)
            new_max = clip->min_time + min_length;
        return {
            .min = clip->min_time,
            .max = new_max,
            .start_offset = 0.0,
        };
    }

    const double old_min = clip->min_time;
    double new_min = math::max(clip->min_time + relative_pos, 0.0);
    if (new_min >= clip->max_time)
        new_min = clip->max_time - min_length;

    double start_offset = clip->start_offset;
    SampleAsset* asset = nullptr;
    if (clip->is_audio()) {
        asset = clip->audio.asset;
        start_offset = samples_to_beat(start_offset, (double)asset->sample_instance.sample_rate,
                                       beat_duration);
    }

    if (old_min < new_min)
        start_offset -= old_min - new_min;
    else
        start_offset += new_min - old_min;

    if (start_offset < 0.0)
        new_min = new_min - start_offset;

    start_offset = math::max(start_offset, 0.0);

    if (clip->is_audio()) {
        start_offset = beat_to_samples(start_offset, (double)asset->sample_instance.sample_rate,
                                       beat_duration);
    }

    return {
        .min = new_min,
        .max = clip->max_time,
        .start_offset = start_offset,
    };
}

static inline double calc_shift_clip(Clip* clip, double relative_pos) {
    const double rel_offset = clip->start_offset;
    return math::max(rel_offset - relative_pos, 0.0);
}

static inline double shift_clip_content(Clip* clip, double relative_pos, double beat_duration) {
    if (clip->is_audio()) {
        SampleAsset* asset = clip->audio.asset;
        const double sample_rate = (double)asset->sample_instance.sample_rate;
        const double rel_offset = samples_to_beat(clip->start_offset, sample_rate, beat_duration);
        return beat_to_samples(math::max(rel_offset - relative_pos, 0.0), sample_rate,
                               beat_duration);
    }

    const double rel_offset = clip->start_offset;
    return math::max(rel_offset - relative_pos, 0.0);
}
} // namespace wb
