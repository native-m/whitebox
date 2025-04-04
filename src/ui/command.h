#pragma once

#include <string>

#include "core/common.h"
#include "core/color.h"
#include "core/list.h"
#include "engine/clip.h"
#include "engine/etypes.h"

namespace wb {

struct TrackHistory {
  Vector<Clip> deleted_clips;
  Vector<Clip> added_clips;
  Vector<uint32_t> modified_clips;

  void backup(TrackEditResult&& edit_result);
  void undo(Track* track);
};

struct TimelineHistory { };

struct Command : public InplaceList<Command> {
  std::string name;
  virtual ~Command() {
  }
  virtual void execute() = 0;
  virtual void undo() = 0;
};

struct TrackAddCmd : public Command {
  Color color;
  uint32_t track_id;

  void execute() override;
  void undo() override;
};

struct TrackDeleteCmd { };

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
  Color old_color;
  Color new_color;

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

struct ClipCmd : public Command {
  Vector<Pair<uint32_t, Clip>> deleted_clips;
  Vector<Pair<uint32_t, uint32_t>> added_clips;
  Vector<Pair<uint32_t, uint32_t>> modified_clips;

  void backup(MultiEditResult&& edit_result);
  void undo(uint32_t begin_track, uint32_t end_track);
  void clean_edit_result();
};

struct CreateMidiClipCmd : public ClipCmd {
  Vector<SelectedTrackRegion> selected_track_regions;
  uint32_t first_track;
  double min_pos;
  double max_pos;

  void execute() override;
  void undo() override;
};

struct ClipMoveCmd2 : public ClipCmd {
  Vector<SelectedTrackRegion> selected_track_regions;
  uint32_t src_track_idx;
  int32_t dst_track_relative_idx;
  double min_pos;
  double max_pos;
  double relative_move_pos;
  bool duplicate;

  void execute() override;
  void undo() override;
};

struct ClipResizeCmd2 : public ClipCmd {
  Vector<TrackClipResizeInfo> track_clip;
  uint32_t first_track;
  double relative_pos;
  double resize_limit;
  double min_length;
  double min_resize_pos;
  bool right_side;
  bool shift;

  void execute() override;
  void undo() override;
};

struct ClipDeleteCmd2 : public ClipCmd {
  Vector<SelectedTrackRegion> selected_track_regions;
  uint32_t first_track;
  double min_pos;
  double max_pos;

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

}  // namespace wb