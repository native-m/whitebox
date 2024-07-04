#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/math.h"

namespace wb {
struct ClipMoveResult {
    double min;
    double max;
};

struct ClipResizeResult {
    double min;
    double max;
    double rel_offset;
};

static inline ClipMoveResult calc_move_clip(Clip* clip, double relative_pos) {
    double new_pos = math::max(clip->min_time + relative_pos, 0.0);
    return {
        new_pos,
        new_pos + (clip->max_time - clip->min_time),
    };
}

static inline ClipResizeResult calc_resize_clip(Clip* clip, double relative_pos, double min_length,
                                                bool is_min) {
    if (!is_min) {
        double new_max = math::max(clip->max_time + relative_pos, 0.0);
        if (new_max <= clip->min_time)
            new_max = clip->min_time + min_length;
        return {
            .min = clip->min_time,
            .max = new_max,
            .rel_offset = clip->relative_start_time,
        };
    }

    double old_min = clip->min_time;
    double new_min = math::max(clip->min_time + relative_pos, 0.0);
    if (new_min >= clip->max_time)
        new_min = clip->max_time - min_length;

    double rel_offset = clip->relative_start_time;
    if (old_min < new_min)
        rel_offset -= old_min - new_min;
    else
        rel_offset += new_min - old_min;

    if (rel_offset < 0.0)
        new_min = new_min - rel_offset;

    return {
        .min = new_min,
        .max = clip->max_time,
        .rel_offset = math::max(rel_offset, 0.0),
    };
}
} // namespace wb
