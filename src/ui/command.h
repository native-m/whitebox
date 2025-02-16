#pragma once

#include "core/common.h"
#include "engine/assets_table.h"
#include "engine/clip.h"
#include "engine/etypes.h"
#include <string>

namespace wb {

struct TrackHistory {
    Vector<Clip> deleted_clips;
    Vector<Clip> added_clips;
    Vector<uint32_t> modified_clips;

    void backup(TrackEditResult&& edit_result);
    void undo(Track* track);
};

struct EmptyCmd {
    void execute() {}
    void undo() {}
};

struct TrackMoveCmd {
    uint32_t src_slot;
    uint32_t dst_slot;

    void execute();
    void undo();
};

struct ClipAddFromFileCmd {
    uint32_t track_id;
    double cursor_pos;
    std::filesystem::path file;
    TrackHistory history;
    Clip new_clip;

    void execute();
    void undo();
};

struct ClipMoveCmd {
    uint32_t src_track_id;
    uint32_t dst_track_id;
    uint32_t clip_id;
    double relative_pos;
    TrackHistory src_track_history;
    TrackHistory dst_track_history;

    void execute();
    void undo();
};

struct ClipShiftCmd {
    uint32_t track_id;
    uint32_t clip_id;
    double relative_pos;
    double last_beat_duration;

    void execute();
    void undo();
};

struct ClipResizeCmd {
    uint32_t track_id;
    uint32_t clip_id;
    bool left_side;
    bool shift;
    double relative_pos;
    double min_length;
    double last_beat_duration;
    TrackHistory history;

    void execute();
    void undo();
};

struct ClipDuplicateCmd {
    uint32_t src_track_id;
    uint32_t dst_track_id;
    uint32_t clip_id;
    double relative_pos;
    TrackHistory track_history;

    void execute();
    void undo();
};

struct ClipDeleteCmd {
    uint32_t track_id;
    uint32_t clip_id;
    TrackHistory history;

    void execute();
    void undo();
};

struct ClipDeleteRegionCmd {
    uint32_t first_track_id;
    uint32_t last_track_id;
    double min_time;
    double max_time;
    Vector<TrackHistory> histories;

    void execute();
    void undo();
};

struct TrackParameterChangeCmd {
    uint32_t track_id;
    uint16_t param_id;
    uint16_t plugin_id;
    uint32_t plugin_param_id;
    double old_value;
    double new_value;
};

} // namespace wb