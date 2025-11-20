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
  TrackID src = reverse ? dst_slot : src_slot;
  TrackID dst = reverse ? src_slot : dst_slot;

  Engine2::begin_edit();
  Track* tmp = Engine2::tracks[src];

  if (src < dst) {
    for (TrackID i = src; i < dst; i++)
      Engine2::tracks[i] = Engine2::tracks[i + 1];
  } else {
    for (TrackID i = src; i > dst; i--)
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

//

bool CmdRenameTrack::execute() {
  Track* track = Engine2::tracks[track_id];
  track->name = new_name;
  return true;
}

void CmdRenameTrack::undo() {
  Track* track = Engine2::tracks[track_id];
  track->name = old_name;
}

//

bool CmdChangeTrackColor::execute() {
  Track* track = Engine2::tracks[track_id];
  track->color = new_color;
  return true;
}

void CmdChangeTrackColor::undo() {
  Track* track = Engine2::tracks[track_id];
  track->color = old_color;
}

//

bool CmdApplyTrackColorToClips::execute() {
  for (auto track_id : track_ids) {
    Track* track = Engine2::tracks[track_id];
    for (auto clip : track->clips) {
      backup_colors.push_back({
        .track_id = track_id,
        .clip_id = clip->id,
        .color = clip->color.to_uint32(),
      });
      clip->color = track->color;
    }
  }
  return true;
}

void CmdApplyTrackColorToClips::undo() {
  for (const auto& backup_color : backup_colors) {
    Track* track = Engine2::tracks[backup_color.track_id];
    Clip* clip = track->clips[backup_color.clip_id];
    clip->color = backup_color.color;
  }
  backup_colors.resize(0);
}

}  // namespace wb