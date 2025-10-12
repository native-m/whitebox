#pragma once

#include <filesystem>

#include "clip.h"
#include "command2.h"
#include "core/span.h"
#include "etypes.h"

namespace wb {

struct CmdClip : public Command2 {
  Vector<Pair<int32_t, Vector<Clip>>> track_backups;

  void add_track_backup(int32_t track_id);

  void delete_region(const ClipSpan& clip_span, Track* track, double start_pos, double end_pos, double beat_duration);

  void delete_regions(
      const Vector<ClipSpan>& selected_track_regions,
      int32_t first_track_idx,
      double start_pos,
      double end_pos,
      double beat_duration);

  void undo();
};

struct CmdAddClipFromFile final : public CmdClip {
  int32_t track_id;
  double position;
  std::filesystem::path file_path;

  bool execute() override;
  void undo() override;
};

struct CmdDeleteSelectedRegion final : public CmdClip {
  int32_t first_track;
  Vector<ClipSpan> clip_spans;
  double start_pos;
  double end_pos;

  bool execute() override;
  void undo() override;
};

}  // namespace wb