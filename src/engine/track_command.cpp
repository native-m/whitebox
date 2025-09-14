#include "track_command.h"

#include "core/defer.h"
#include "engine2.h"

namespace wb {

bool CmdAddTrack::execute() {
  Engine2::create_track(name, color, 60.0f);
  track_id = Engine2::tracks.size() - 1;
  return true;
}

void CmdAddTrack::undo() {
  Engine2::delete_track(track_id);
}

bool CmdMoveTrack::execute() {
  if (src_slot == dst_slot)
    return false;
  move(false);
  return true;
}

void CmdMoveTrack::undo() {
  move(true);
}

void CmdMoveTrack::move(bool reverse) {
  uint32_t src = reverse ? dst_slot : src_slot;
  uint32_t dst = reverse ? src_slot : dst_slot;

  Engine2::begin_edit();
  Track* tmp = Engine2::tracks[src];

  if (src < dst) {
    for (uint32_t i = src; i < dst; i++)
      Engine2::tracks[i] = Engine2::tracks[i + 1];
  } else {
    for (uint32_t i = src; i > dst; i--)
      Engine2::tracks[i] = Engine2::tracks[i - 1];
  }

  Engine2::tracks[dst] = tmp;
  Engine2::end_edit();
}

}  // namespace wb