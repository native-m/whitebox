#include <string>

#include "command2.h"
#include "core/color.h"
#include "core/vector.h"
#include "engine/track.h"

namespace wb {

struct TrackBackup {
  int32_t id;
  ColorU32 color;
  float height;
  std::string name;
  TrackParameterState param_state;
  Vector<Clip> clips;
  bool shown;
};

struct CmdAddTrack final : public Command2 {
  std::string name;
  Color color;
  int32_t track_id;

  bool execute() override;
  void undo() override;
};

struct CmdMoveTrack final : public Command2 {
  int32_t src_slot;
  int32_t dst_slot;

  bool execute() override;
  void undo() override;
  void move(bool reverse);
};

struct CmdDeleteTrack final : public Command2 {
  Vector<int32_t> track_ids;
  Vector<TrackBackup> backups;

  bool execute() override;
  void undo() override;
};

struct CmdApplyTrackColorToClips : public Command2 {
  struct ColorStorage {
    int32_t track_id;
    uint32_t clip_id;
    Color colors;
  };

  int32_t first_track;
  Vector<Color> track_color;
  Vector<ColorStorage> clip_colors;

  bool execute() override;
  void undo() override;
};

}  // namespace wb