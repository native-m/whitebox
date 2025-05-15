#pragma once

#include <string>

#include "core/color.h"
#include "core/common.h"
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

struct Command : public InplaceList<Command> {
  std::string name;
  virtual ~Command() {
  }
  virtual bool execute() = 0;
  virtual void undo() = 0;
};

struct TrackAddCmd : public Command {
  Color color;
  uint32_t track_id;

  bool execute() override;
  void undo() override;
};

struct TrackMoveCmd : public Command {
  uint32_t src_slot;
  uint32_t dst_slot;

  bool execute() override;
  void undo() override;
};

struct ClipAddFromFileCmd : public Command {
  uint32_t track_id;
  double cursor_pos;
  std::filesystem::path file;
  TrackHistory history;
  Clip new_clip;

  bool execute() override;
  void undo() override;
};

struct ClipRenameCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  std::string old_name;
  std::string new_name;

  bool execute() override;
  void undo() override;
};

struct ClipChangeColorCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  Color old_color;
  Color new_color;

  bool execute() override;
  void undo() override;
};

struct ClipMoveCmd : public Command {
  uint32_t src_track_id;
  uint32_t dst_track_id;
  uint32_t clip_id;
  double relative_pos;
  TrackHistory src_track_history;
  TrackHistory dst_track_history;

  bool execute() override;
  void undo() override;
};

struct ClipShiftCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  double relative_pos;
  double last_beat_duration;

  bool execute() override;
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

  bool execute() override;
  void undo() override;
};

struct ClipDuplicateCmd : public Command {
  uint32_t src_track_id;
  uint32_t dst_track_id;
  uint32_t clip_id;
  double relative_pos;
  TrackHistory track_history;

  bool execute() override;
  void undo() override;
};

struct ClipDeleteCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  TrackHistory history;

  bool execute() override;
  void undo() override;
};

struct ClipDeleteRegionCmd : public Command {
  uint32_t first_track_id;
  uint32_t last_track_id;
  double min_time;
  double max_time;
  Vector<TrackHistory> histories;

  bool execute() override;
  void undo() override;
};

struct ClipAdjustGainCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  float gain_before;
  float gain_after;

  bool execute() override;
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

  bool execute() override;
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

  bool execute() override;
  void undo() override;
};

struct ClipResizeCmd2 : public ClipCmd {
  Vector<TrackClipResizeInfo> track_clip;
  uint32_t first_track;
  double relative_pos;
  double resize_limit;
  double min_length;
  double min_resize_pos;
  bool left_side;
  bool shift;

  bool execute() override;
  void undo() override;
};

struct ClipDeleteCmd2 : public ClipCmd {
  Vector<SelectedTrackRegion> selected_track_regions;
  uint32_t first_track;
  double min_pos;
  double max_pos;

  bool execute() override;
  void undo() override;
};

struct MidiCmd : public Command {
  Vector<uint32_t> modified_notes;
  Vector<MidiNote> deleted_notes;

  void backup(MidiEditResult&& edit_result);
  void undo(uint32_t track_id, uint32_t clip_id, uint32_t channel);
};

struct MidiAddNoteCmd : public MidiCmd {
  uint32_t track_id;
  uint32_t clip_id;
  double min_time;
  double max_time;
  float velocity;
  uint16_t note_key;
  uint16_t channel;

  bool execute() override;
  void undo() override;
};

struct MidiPaintNotesCmd : public MidiCmd {
  uint32_t track_id;
  uint32_t clip_id;
  Vector<MidiNote> notes;
  uint16_t channel;

  bool execute() override;
  void undo() override;
};

struct MidiSliceNoteCmd : public MidiCmd {
  uint32_t track_id;
  uint32_t clip_id;
  double pos;
  float velocity;
  uint16_t note_key;
  uint16_t channel;

  bool execute() override;
  void undo() override;
};

struct MidiMoveNoteCmd : public MidiCmd {
  uint32_t track_id;
  uint32_t clip_id;
  uint32_t note_id;  // Valid only if move_selected is true
  bool move_selected;
  double relative_pos;
  uint16_t relative_key_pos;

  bool execute() override;
  void undo() override;
};

struct MidiSelectNoteCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  double min_pos;
  double max_pos;
  uint16_t min_key;
  uint16_t max_key;
  NoteSelectResult result;

  bool execute() override;
  void undo() override;
};

struct MidiSelectOrDeselectNotesCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  bool should_select;
  NoteSelectResult result;

  bool execute() override;
  void undo() override;
};

struct MidiAppendNoteSelectionCmd : public Command {
  uint32_t track_id;
  uint32_t clip_id;
  bool select_or_deselect;
  Vector<uint32_t> selected_note_ids;

  bool execute() override;
  void undo() override;
};

struct MidiDeleteNoteCmd : public MidiCmd {
  uint32_t track_id;
  uint32_t clip_id;
  bool selected;

  bool execute() override;
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
