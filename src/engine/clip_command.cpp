#include "clip_command.h"

#include "asset.h"
#include "core/span.h"
#include "engine2.h"
#include "track.h"

namespace wb {

void CmdClip::add_track_backup(int32_t track_id) {
  Track* track = Engine2::tracks[track_id];
  auto& track_backup = track_backups.emplace_back(track_id);
  for (auto& clip : track->clips) {
    track_backup.second.emplace_back(*clip);
  }
}

void CmdClip::delete_region(const ClipSpan& clip_span, int32_t track_idx, double start_pos, double end_pos) {
}

void CmdClip::delete_region(
    const Vector<ClipSpan>& selected_track_regions,
    int32_t first_track_idx,
    double start_pos,
    double end_pos) {
}

void CmdClip::undo() {
  for (auto& [track_id, clip_backups] : track_backups) {
    Track* track = Engine2::tracks[track_id];
    uint32_t count = math::min(track->clips.size(), clip_backups.size());
    uint32_t i = 0;

    for (auto clip : track->clips) {
      Engine2::destroy_clip(clip);
    }

    for (; i < count; i++) {
      Clip* restored_clip = Engine2::allocate_clip();
      new (restored_clip) Clip(std::move(clip_backups[i]));
      track->clips[i] = restored_clip;
    }

    track->clips.resize(clip_backups.size());

    if (clip_backups.size() > track->clips.size()) {
      for (; i < clip_backups.size(); i++) {
        Clip* restored_clip = Engine2::allocate_clip();
        new (restored_clip) Clip(std::move(clip_backups[i]));
        track->clips[i] = restored_clip;
      }
    }
  }
}

bool CmdAddClipFromFile::execute() {
  Track* track = Engine2::tracks[track_id];

  if (auto asset = AssetManager::create_or_get_audio_asset(file_path.generic_string())) {
    double ppq = Engine2::get_ppq();
    double beat_duration = Engine2::get_beat_duration();
    double sample_rate = asset->sample.sample_rate;
    double clip_length = samples_to_beat(asset->sample.count, sample_rate, beat_duration);
    double end_pos = position + math::round(clip_length * ppq) / ppq;
    Clip* clip = Engine2::create_clip(file_path.filename().string(), track->color, position, end_pos);

    clip->init_as_audio_clip({
      .asset = asset,
      .speed = 1.0,
      .gain = 1.0f,
    });

    Engine2::begin_edit();
    add_track_backup(track_id);
    track->clips.push_back(clip);
    Engine2::update_track_state(track);
    Engine2::end_edit();
  }

  return true;
}

void CmdAddClipFromFile::undo() {
  Engine2::begin_edit();
  CmdClip::undo();
  Engine2::end_edit();
}

}  // namespace wb