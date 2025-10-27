#pragma once

#include <filesystem>

#include "clip.h"
#include "command2.h"
#include "core/span.h"
#include "engine/clip_edit.h"
#include "etypes.h"

namespace wb {

struct CmdClip : public Command2 {
  Vector<Pair<int32_t, Clip>> deleted_clips;
  Vector<std::tuple<int32_t, uint32_t, Clip*>> added_clips;
  Vector<uint32_t> modified_tracks;

  void delete_region(
      const ClipSpan& clip_span,
      Track* track,
      uint32_t track_id,
      double start_pos,
      double end_pos,
      double beat_duration,
      Clip* excluded_clip = nullptr);

  void resolve_id_for_added_clips();

  void restore_clip_backups();

  void undo() override;
};

struct CmdAddClipFromFile final : public CmdClip {
  int32_t track_id;
  double position;
  std::filesystem::path file_path;

  bool execute() override;
};

struct CmdAddMidiClips final : public CmdClip {
  int32_t first_track;
  int32_t last_track;
  double start_pos;
  double end_pos;

  bool execute() override;
};

struct CmdMoveClips final : public CmdClip {
  int32_t src_track_id;
  int32_t dst_track_relative_ofs;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;
  double relative_move_ofs;
  bool duplicate;

  bool execute() override;
};

struct CmdResizeClips final : public CmdClip {
  int32_t first_track;
  Vector<TrackClipResizeInfo> clips;
  double relative_ofs;
  double min_clip_length;
  double min_relative_ofs;
  ClipResizeMode mode;
  bool left_side;

  bool execute() override;
};

struct CmdDeleteClips final : public CmdClip {
  int32_t first_track;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;

  bool execute() override;
};

struct CmdDeleteClip final : public CmdClip {
  int32_t track_id;
  uint32_t clip_id;

  bool execute() override;
};

struct CmdRenameClip final : public Command2 {
  int32_t track_id;
  uint32_t clip_id;
  std::string new_name;
  std::string old_name;

  bool execute() override;
  void undo() override;
};

}  // namespace wb