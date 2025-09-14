#include <string>

#include "command2.h"

namespace wb {

struct CmdAddTrack final : public Command2 {
  std::string name;
  Color color;
  uint32_t track_id;

  bool execute() override;
  void undo() override;
};

struct CmdDeleteTrack final : public Command2 {
  bool execute() override;
  void undo() override;
};

struct CmdMoveTrack final : public Command2 {
  uint32_t src_slot;
  uint32_t dst_slot;

  bool execute() override;
  void undo() override;
  void move(bool reverse);
};

}  // namespace wb