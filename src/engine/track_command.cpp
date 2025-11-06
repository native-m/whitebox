#include "track_command.h"

#include "core/defer.h"
#include "engine2.h"

namespace wb {

bool CmdAddTrack::execute() {
  Engine2::add_track(name, color, 60.0f);
  track_id = Engine2::tracks.size() - 1;
  return true;
}

void CmdAddTrack::undo() {
  Engine2::delete_track(track_id);
}

//

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

//

bool CmdDeleteTrack::execute() {
  for (const auto track_id : track_ids) {
    Track* track = Engine2::tracks[track_id];
    TrackBackup& backup = backups.emplace_back();

    backup.id = track_id;
    backup.color = track->color.to_uint32();
    backup.height = track->height;
    backup.name = std::move(track->name);
    backup.param_state = track->ui_parameter_state;
    backup.clips.reserve(track->clips.size());

    for (const auto clip : track->clips) {
      backup.clips.push_back(*clip);
    }
  }

  Engine2::begin_edit();

  for (const auto track_id : track_ids) {
    Engine2::delete_track(track_id);
  }

  Engine2::end_edit();

  return true;
}

void CmdDeleteTrack::undo() {
  Engine2::begin_edit();

  for (auto& track : backups) {
    Track* restored_track =
        Engine2::create_track(track.name, track.color, track.height, track.param_state.volume_db, track.param_state.pan);
    restored_track->set_mute(track.param_state.mute);
    restored_track->ui_parameter_state.solo = track.param_state.solo;
    
    for (auto& clip : track.clips) {
      Clip* restored_clip = Engine2::allocate_clip();
      new (restored_clip) Clip(std::move(clip));
      restored_track->clips.push_back(restored_clip);
    }

    Engine2::tracks.emplace_at(track.id, restored_track);
    Engine2::update_track_state(restored_track);
  }

  Engine2::end_edit();
  backups.resize(0);
}

}  // namespace wb