#pragma once

#include <filesystem>

#include "clip.h"
#include "command2.h"
#include "core/span.h"
#include "etypes.h"

namespace wb {

struct CmdClip : public Command2 {
  Vector<Pair<uint32_t, Vector<Clip>>> track_backups; // Would be nice if we have InplaceVector

  void add_track_backup(int32_t track_id);

  void delete_region();

  void delete_region(
      const Vector<ClipQueryResult2>& selected_track_regions,
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