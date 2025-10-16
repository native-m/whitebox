#include "clip_command.h"

#include "asset.h"
#include "clip_edit.h"
#include "core/span.h"
#include "engine2.h"
#include "track.h"

namespace wb {

void CmdClip::backup_track_clips(int32_t track_id) {
  Track* track = Engine2::tracks[track_id];
  auto& track_backup = track_backups.emplace_back(track_id);
  for (auto& clip : track->clips) {
    track_backup.second.emplace_back(*clip);
  }
}

void CmdClip::delete_region(
    const ClipSpan& clip_span,
    Track* track,
    double start_pos,
    double end_pos,
    double beat_duration) {
  for (uint32_t i = clip_span.first; i <= clip_span.last; i++) {
    Clip* clip = track->clips[i];
    if (clip_span.left_side_partially_selected(i) && clip_span.right_side_partially_selected(i)) {
      double shift_amount = clip->min_time - end_pos;
      Clip* new_clip = Engine2::allocate_clip();
      new (new_clip) Clip(*clip);
      new_clip->start_offset = shift_clip_content(clip, shift_amount, beat_duration);
      new_clip->min_time = end_pos;
      clip->max_time = start_pos;
      track->clips.push_back(new_clip);
    } else if (clip_span.left_side_partially_selected(i)) {
      double shift_amount = clip->min_time - end_pos;
      clip->start_offset = shift_clip_content(clip, shift_amount, beat_duration);
      clip->min_time = end_pos;
    } else if (clip_span.right_side_partially_selected(i)) {
      clip->max_time = start_pos;
    } else if (!clip->deleted) {
      track->mark_clip_deleted(clip);
    }
  }
}

void CmdClip::delete_regions(
    const Vector<ClipSpan>& selected_track_regions,
    int32_t first_track_idx,
    double start_pos,
    double end_pos,
    double beat_duration) {
  for (int32_t i = 0; const auto& clip_span : selected_track_regions) {
    delete_region(clip_span, Engine2::tracks[first_track_idx + i], start_pos, end_pos, beat_duration);
    i++;
  }
}

void CmdClip::undo() {
  for (auto& [track_id, clip_backups] : track_backups) {
    Track* track = Engine2::tracks[track_id];
    uint32_t count = clip_backups.size();
    uint32_t i = 0;

    for (auto clip : track->clips) {
      Engine2::destroy_clip(clip);
    }

    track->clips.resize(count);

    for (; i < count; i++) {
      Clip* restored_clip = Engine2::allocate_clip();
      new (restored_clip) Clip(std::move(clip_backups[i]));
      track->clips[i] = restored_clip;
    }
  }
}

//

bool CmdAddClipFromFile::execute() {
  Track* track = Engine2::tracks[track_id];
  std::string name = file_path.filename().string();
  double beat_duration = Engine2::get_beat_duration();

  if (auto asset = AssetManager::create_or_get_audio_asset(file_path.generic_string())) {
    double ppq = Engine2::get_ppq();
    double sample_rate = asset->sample.sample_rate;
    double clip_length = samples_to_beat(asset->sample.count, sample_rate, beat_duration);
    double end_pos = position + math::round(clip_length * ppq) / ppq;
    Clip* clip = Engine2::create_clip(name, track->color, position, end_pos);

    clip->init_as_audio_clip({
      .asset = asset,
      .speed = 1.0,
      .gain = 1.0f,
    });

    auto clip_span = track->query_clip_by_range2(position, end_pos);

    Engine2::begin_edit();
    backup_track_clips(track_id);

    if (clip_span)
      delete_region(clip_span, track, position, end_pos, beat_duration);

    track->clips.push_back(clip);
    Engine2::update_track_state(track);
    Engine2::end_edit();
  } else if (auto asset = AssetManager::create_midi_asset_from_file(file_path.generic_string())) {
    double end_pos = position + asset->data.max_length;
    Clip* clip = Engine2::create_clip(name, track->color, position, end_pos);
    clip->init_as_midi_clip({ .asset = asset, .length = asset->data.max_length, .rate = 1 });

    auto clip_span = track->query_clip_by_range2(position, end_pos);

    Engine2::begin_edit();
    backup_track_clips(track_id);

    if (clip_span)
      delete_region(clip_span, track, position, end_pos, beat_duration);

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

//

bool CmdMoveClips::execute() {
  int32_t dst_track_id = src_track_id + dst_track_relative_pos;
  int32_t src_track_end = src_track_id + clip_spans.size();
  int32_t dst_track_end = dst_track_id + clip_spans.size();
  double beat_duration = Engine2::get_beat_duration();

  Engine2::begin_edit();
  if (dst_track_id >= src_track_id && dst_track_id <= src_track_end) {
    // bool overlapped_pos;
    // Overlapped tracks case
    for (int32_t track_id = src_track_id; track_id <= dst_track_end; track_id++) {
      backup_track_clips(track_id);
    }
  } else {
    for (int32_t i = 0; i <= clip_spans.size(); i++) {
      int32_t track_id = src_track_id + i;
      Track* track = Engine2::tracks[track_id];
      const ClipSpan& clip_span = clip_spans[i];
      backup_track_clips(track_id);
      delete_region(clip_span, track, start_pos, end_pos, beat_duration);
      Engine2::update_track_state(track);
    }

    for (int32_t i = 0; i <= clip_spans.size(); i++) {
      int32_t track_id = dst_track_id + i;
      Track* track = Engine2::tracks[track_id];
      const ClipSpan& clip_span = clip_spans[i];
      backup_track_clips(track_id);
      delete_region(clip_span, track, start_pos, end_pos, beat_duration);
      Engine2::update_track_state(track);
    }
  }
  Engine2::end_edit();

  return false;
}

void CmdMoveClips::undo() {
}

//

bool CmdDeleteClips::execute() {
  if (clip_spans.size() == 0)
    return false;

  double beat_duration = Engine2::get_beat_duration();
  Engine2::begin_edit();
  for (int32_t i = first_track; const auto& clip_span : clip_spans) {
    Track* track = Engine2::tracks[i];
    backup_track_clips(i);
    delete_region(clip_span, track, start_pos, end_pos, beat_duration);
    Engine2::update_track_state(track);
    i++;
  }
  Engine2::end_edit();

  return true;
}

void CmdDeleteClips::undo() {
  Engine2::begin_edit();
  CmdClip::undo();
  Engine2::end_edit();
}

}  // namespace wb