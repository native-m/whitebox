#pragma once

#include "core/common.h"

namespace wb
{
struct ClipMoveCmd {
    uint32_t track_id;
    uint32_t clip_id;
    double relative_pos;

    void execute();
    void undo();
};

struct ClipShiftCmd {
    uint32_t track_id;
    uint32_t clip_id;
    double relative_pos;

    void execute();
    void undo();
};

struct ClipResizeCmd {
    uint32_t track_id;
    uint32_t clip_id;
    bool left_side;
    double relative_pos;
    double min_length;

    void execute();
    void undo();
};
}