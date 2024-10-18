#pragma once

#include "core/common.h"
#include "core/vector.h"

namespace wb {
struct Clip;
struct Track;

struct ClipMoveResult {
    double min;
    double max;
};

struct ClipResizeResult {
    double min;
    double max;
    double start_offset;
};

struct ClipQueryResult {
    uint32_t first;
    uint32_t last;
    double first_offset;
    double last_offset;
};

struct TrackEditResult {
    Vector<Clip> deleted_clips;
    Vector<Clip*> added_clips;
    Vector<Clip*> modified_clips;
    Clip* new_clip;
};

} // namespace wb