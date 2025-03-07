#pragma once

#include "core/common.h"
#include "core/list.h"
#include "engine/clip.h"
#include "engine/etypes.h"
#include <imgui.h>
#include <string>

namespace wb {

struct TrackHistory {
    Vector<Clip> deleted_clips;
    Vector<Clip> added_clips;
    Vector<uint32_t> modified_clips;

    void backup(TrackEditResult&& edit_result);
    void undo(Track* track);
};

struct Command : public InplaceList<Command> {
    std::string name;
    virtual ~Command() {}
    virtual void execute() = 0;
    virtual void undo() = 0;
};

struct TrackAddCmd : public Command {
    ImColor color;
    uint32_t track_id;

    void execute() override;
    void undo() override;
};

struct TrackDeleteCmd {};

struct TrackMoveCmd : public Command {
    uint32_t src_slot;
    uint32_t dst_slot;

    void execute() override;
    void undo() override;
};

struct ClipAddFromFileCmd : public Command {
    uint32_t track_id;
    double cursor_pos;
    std::filesystem::path file;
    TrackHistory history;
    Clip new_clip;

    void execute() override;
    void undo() override;
};

struct ClipRenameCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    std::string old_name;
    std::string new_name;

    void execute() override;
    void undo() override;
};

struct ClipChangeColorCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    ImColor old_color;
    ImColor new_color;

    void execute() override;
    void undo() override;
};

struct ClipMoveCmd : public Command {
    uint32_t src_track_id;
    uint32_t dst_track_id;
    uint32_t clip_id;
    double relative_pos;
    TrackHistory src_track_history;
    TrackHistory dst_track_history;

    void execute() override;
    void undo() override;
};

struct ClipShiftCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    double relative_pos;
    double last_beat_duration;

    void execute() override;
    void undo() override;
};

struct ClipResizeCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    bool left_side;
    bool shift;
    double relative_pos;
    double min_length;
    double last_beat_duration;
    TrackHistory history;

    void execute() override;
    void undo() override;
};

struct ClipDuplicateCmd : public Command {
    uint32_t src_track_id;
    uint32_t dst_track_id;
    uint32_t clip_id;
    double relative_pos;
    TrackHistory track_history;

    void execute() override;
    void undo() override;
};

struct ClipDeleteCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    TrackHistory history;

    void execute() override;
    void undo() override;
};

struct ClipDeleteRegionCmd : public Command {
    uint32_t first_track_id;
    uint32_t last_track_id;
    double min_time;
    double max_time;
    Vector<TrackHistory> histories;

    void execute() override;
    void undo() override;
};

struct ClipAdjustGainCmd : public Command {
    uint32_t track_id;
    uint32_t clip_id;
    float gain_before;
    float gain_after;

    void execute() override;
    void undo() override;
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