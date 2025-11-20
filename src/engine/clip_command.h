#pragma once

#include <filesystem>

#include "clip.h"
#include "command2.h"
#include "core/span.h"
#include "engine/clip_edit.h"
#include "etypes.h"

namespace wb {

struct CmdClip : public Command2 {
  Vector<Pair<TrackID, Clip>> deleted_clips;
  Vector<std::tuple<TrackID, ClipID, Clip*>> added_clips;
  Vector<TrackID> modified_tracks;

  void delete_region(
      const ClipSpan& clip_span,
      Track* track,
      TrackID track_id,
      double start_pos,
      double end_pos,
      double beat_duration,
      Clip* excluded_clip = nullptr);

  void resolve_id_for_added_clips();

  void restore_clip_backups();

  void undo() override;
};

struct CmdAddClipFromFile final : public CmdClip {
  TrackID track_id;
  double position;
  std::filesystem::path file_path;

  bool execute() override;
};

struct CmdAddMidiClips final : public CmdClip {
  TrackID first_track;
  TrackID last_track;
  double start_pos;
  double end_pos;

  bool execute() override;
};

struct CmdMoveClips final : public CmdClip {
  TrackID src_track_id;
  int32_t dst_track_relative_ofs;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;
  double relative_move_ofs;
  bool duplicate;

  bool execute() override;
};

struct CmdResizeClips final : public CmdClip {
  TrackID first_track;
  Vector<TrackClipResizeInfo> clips;
  double relative_ofs;
  double min_clip_length;
  double min_relative_ofs;
  ClipResizeMode mode;
  bool left_side;

  bool execute() override;
};

struct CmdDeleteClips final : public CmdClip {
  TrackID first_track;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;

  bool execute() override;
};

struct CmdDeleteClip final : public Command2 {
  TrackID track_id;
  uint32_t clip_id;
  std::optional<Clip> backup_clip;

  bool execute() override;
  void undo() override;
};

struct CmdRenameClip final : public Command2 {
  TrackID track_id;
  uint32_t clip_id;
  std::string new_name;
  std::string old_name;

  bool execute() override;
  void undo() override;
};

}  // namespace wb