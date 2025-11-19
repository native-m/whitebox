#include <string>

#include "command2.h"
#include "core/color.h"
#include "core/vector.h"
#include "engine/track.h"

namespace wb {

struct TrackBackup {
  TrackID id;
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
  TrackID track_id;

  bool execute() override;
  void undo() override;
};

struct CmdMoveTrack final : public Command2 {
  TrackID src_slot;
  TrackID dst_slot;

  bool execute() override;
  void undo() override;
  void move(bool reverse);
};

struct CmdDeleteTrack final : public Command2 {
  Vector<TrackID> track_ids;
  Vector<TrackBackup> backups;

  bool execute() override;
  void undo() override;
};

struct CmdRenameTrack : public Command2 {
  TrackID track_id;
  std::string new_name;
  std::string old_name;
  
  bool execute() override;
  void undo() override;
};

struct CmdChangeTrackColor : public Command2 {
  TrackID track_id;
  ColorU32 new_color;
  ColorU32 old_color;

  bool execute() override;
  void undo() override;
};

struct CmdApplyTrackColorToClips : public Command2 {
  struct ColorStorage {
    TrackID track_id;
    ClipID clip_id;
    ColorU32 color;
  };

  Vector<int32_t> track_ids;
  Vector<ColorStorage> backup_colors;

  bool execute() override;
  void undo() override;
};

}  // namespace wb