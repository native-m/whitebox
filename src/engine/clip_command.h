#pragma once

#include <filesystem>

#include "clip.h"
#include "command2.h"
#include "core/span.h"
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
      double beat_duration);

  void resolve_id_for_added_clips();

  void restore_clip_backups();
};

struct CmdAddClipFromFile final : public CmdClip {
  int32_t track_id;
  double position;
  std::filesystem::path file_path;

  bool execute() override;
  void undo() override;
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
  void undo() override;
};

struct CmdDeleteClips final : public CmdClip {
  int32_t first_track;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;

  bool execute() override;
  void undo() override;
};

}  // namespace wb