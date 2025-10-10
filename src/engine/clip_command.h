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

  void delete_region(const ClipSpan& clip_span, int32_t track_id, double start_pos, double end_pos);

  void delete_region(
      const Vector<ClipSpan>& selected_track_regions,
      int32_t first_track_idx,
      double start_pos,
      double end_pos);

  void undo();
};

struct CmdAddClipFromFile final : public CmdClip {
  int32_t track_id;
  double position;
  std::filesystem::path file_path;

  bool execute() override;
  void undo() override;
};

}  // namespace wb