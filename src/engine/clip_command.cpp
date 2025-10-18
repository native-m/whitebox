#include "clip_command.h"

#include "asset.h"
#include "clip_edit.h"
#include "core/span.h"
#include "engine2.h"
#include "track.h"

namespace wb {

void CmdClip::delete_region(
    const ClipSpan& clip_span,
    Track* track,
    uint32_t track_id,
    double start_pos,
    double end_pos,
    double beat_duration) {
  for (uint32_t i = clip_span.first; i <= clip_span.last; i++) {
    Clip* clip = track->clips[i];
    if (clip_span.left_side_partially_selected(i) && clip_span.right_side_partially_selected(i)) {
      added_clips.emplace_back(track_id, 0, clip);
      deleted_clips.emplace_back(track_id, *clip);
      double shift_amount = clip->min_time - end_pos;
      Clip* new_clip = Engine2::allocate_clip();
      new (new_clip) Clip(*clip);
      new_clip->start_offset = shift_clip_content(clip, shift_amount, beat_duration);
      new_clip->min_time = end_pos;
      clip->max_time = start_pos;
      track->clips.push_back(new_clip);
      added_clips.emplace_back(track_id, 0, new_clip);
    } else if (clip_span.left_side_partially_selected(i)) {
      added_clips.emplace_back(track_id, 0, clip);
      deleted_clips.emplace_back(track_id, *clip);
      double shift_amount = clip->min_time - end_pos;
      clip->start_offset = shift_clip_content(clip, shift_amount, beat_duration);
      clip->min_time = end_pos;
    } else if (clip_span.right_side_partially_selected(i)) {
      added_clips.emplace_back(track_id, 0, clip);
      deleted_clips.emplace_back(track_id, *clip);
      clip->max_time = start_pos;
    } else if (!clip->deleted) {
      deleted_clips.emplace_back(track_id, *clip);
      track->mark_clip_deleted(clip);
    }
  }
}

void CmdClip::resolve_id_for_added_clips() {
  // Resolve IDs for added clips
  for (auto& [_, clip_id, clip] : added_clips) {
    assert(clip);
    clip_id = clip->id;
    clip = nullptr;
  }
}

void CmdClip::restore_clip_backups() {
  // Remove added clips
  for (auto [track_id, clip_id, _] : added_clips) {
    Track* track = Engine2::tracks[track_id];
    Clip* clip = track->clips[clip_id];
    track->mark_clip_deleted(clip);
  }

  // Restore deleted clips
  for (auto& deleted_clip : deleted_clips) {
    Track* track = Engine2::tracks[deleted_clip.first];
    Clip* restored_clip = Engine2::allocate_clip();
    new (restored_clip) Clip(std::move(deleted_clip.second));
    track->clips.push_back(restored_clip);
  }

  for (auto track_id : modified_tracks) {
    Track* track = Engine2::tracks[track_id];
    Engine2::update_track_state(track);
  }

  added_clips.resize(0);
  deleted_clips.resize(0);
  modified_tracks.resize(0);
}

//

bool CmdAddClipFromFile::execute() {
  Track* track = Engine2::tracks[track_id];
  std::string name = file_path.filename().string();
  double beat_duration = Engine2::get_beat_duration();
  double end_pos = 0.0;
  Clip* clip = nullptr;

  if (auto asset = AssetManager::create_or_get_audio_asset(file_path.generic_string())) {
    const double ppq = Engine2::get_ppq();
    const double sample_rate = asset->sample.sample_rate;
    const double clip_length = samples_to_beat(asset->sample.count, sample_rate, beat_duration);
    end_pos = position + math::round(clip_length * ppq) / ppq;
    clip = Engine2::create_clip(name, track->color, position, end_pos);
    clip->init_as_audio_clip({
      .asset = asset,
      .speed = 1.0,
      .gain = 1.0f,
    });
  } else if (auto asset = AssetManager::create_midi_asset_from_file(file_path.generic_string())) {
    end_pos = position + asset->data.max_length;
    clip = Engine2::create_clip(name, track->color, position, end_pos);
    clip->init_as_midi_clip({ .asset = asset, .length = asset->data.max_length, .rate = 1 });
  }

  assert(clip && "Cannot create clip");

  auto clip_span = track->query_clip_by_range2(position, end_pos);
  modified_tracks.push_back(track_id);
  added_clips.emplace_back(track_id, 0, clip);

  Engine2::begin_edit();

  if (clip_span)
    delete_region(clip_span, track, track_id, position, end_pos, beat_duration);

  track->clips.push_back(clip);
  Engine2::update_track_state(track);
  Engine2::end_edit();
  resolve_id_for_added_clips();

  return true;
}

void CmdAddClipFromFile::undo() {
  Engine2::begin_edit();
  CmdClip::restore_clip_backups();
  Engine2::end_edit();
}

//

bool CmdMoveClips::execute() {
  if (dst_track_relative_ofs == 0.0 && relative_move_ofs == 0.0) {
    return false;
  }

  int32_t dst_track_id = src_track_id + dst_track_relative_ofs;
  int32_t num_tracks = (int32_t)clip_spans.size();
  int32_t src_track_end = src_track_id + num_tracks;
  int32_t dst_track_end = dst_track_id + num_tracks;
  int32_t dst_max_bound = Engine2::tracks.size() - num_tracks;
  double start_pos_moved = start_pos + relative_move_ofs;
  double end_pos_moved = end_pos + relative_move_ofs;
  double beat_duration = Engine2::get_beat_duration();
  bool track_overlapped = src_track_id < dst_track_end && dst_track_id < src_track_end;
  Vector<Pair<uint32_t, Clip*>> truncated_clips;

  // auto clear_track_region = [&](Track* track, uint32_t track_id, double clear_start, double clear_end, const
  // ClipQueryResult& query_result, )
  auto clear_track_region = [&](Track* track,
                                uint32_t track_index,
                                double reserve_min,
                                double reserve_max,
                                const ClipSpan& query_result,
                                Clip* last_clip = nullptr) {
    Clip* last_partially_truncated_clip = nullptr;

    for (uint32_t i = query_result.first; i <= query_result.last; i++) {
      Clip* clip = track->clips[i];
      bool right_side_partially_selected = query_result.right_side_partially_selected(i);
      bool left_side_partially_selected = query_result.left_side_partially_selected(i);

      if (right_side_partially_selected && left_side_partially_selected) {
        if (last_clip == nullptr || last_clip->id != clip->id) {
          Clip* left_side_truncated_clip = track->allocate_clip();
          assert(left_side_truncated_clip);
          new (left_side_truncated_clip) Clip(*clip);
          left_side_truncated_clip->max_time = reserve_min;
          truncated_clips.emplace_back(track_index, left_side_truncated_clip);
          added_clips.emplace_back(track_index, 0, left_side_truncated_clip);
        } else {
          last_clip->max_time = reserve_min;
        }

        double right_shift_ofs = clip->min_time - reserve_max;
        Clip* right_side_truncated_clip = track->allocate_clip();
        assert(right_side_truncated_clip);
        new (right_side_truncated_clip) Clip(*clip);
        right_side_truncated_clip->min_time = reserve_max;
        right_side_truncated_clip->start_offset = shift_clip_content(clip, right_shift_ofs, beat_duration);
        truncated_clips.emplace_back(track_index, right_side_truncated_clip);
        added_clips.emplace_back(track_index, 0, right_side_truncated_clip);
        last_partially_truncated_clip = right_side_truncated_clip;
      } else if (right_side_partially_selected) {
        if (last_clip == nullptr || last_clip->id != clip->id) {
          Clip* left_side_truncated_clip = track->allocate_clip();
          assert(left_side_truncated_clip);
          new (left_side_truncated_clip) Clip(*clip);
          left_side_truncated_clip->max_time = reserve_min;
          truncated_clips.emplace_back(track_index, left_side_truncated_clip);
          added_clips.emplace_back(track_index, 0, left_side_truncated_clip);
        } else {
          last_clip->max_time = reserve_min;
        }
      } else if (left_side_partially_selected) {
        double right_shift_ofs = clip->min_time - reserve_max;
        Clip* right_side_truncated_clip = track->allocate_clip();
        assert(right_side_truncated_clip);
        new (right_side_truncated_clip) Clip(*clip);
        right_side_truncated_clip->start_offset = shift_clip_content(clip, right_shift_ofs, beat_duration);
        right_side_truncated_clip->min_time = reserve_max;
        truncated_clips.emplace_back(track_index, right_side_truncated_clip);
        added_clips.emplace_back(track_index, 0, right_side_truncated_clip);
        last_partially_truncated_clip = right_side_truncated_clip;
      }

      if (!clip->deleted) {
        track->mark_clip_deleted(clip);
        deleted_clips.emplace_back(track_index, *clip);
      }
    }

    return last_partially_truncated_clip;
  };

  Engine2::begin_edit();

  // 1. Take out the source region and reserve the destination region
  if (track_overlapped) {
    int32_t first_track = dst_track_relative_ofs >= 0 ? src_track_id : dst_track_id;
    int32_t last_track = dst_track_relative_ofs >= 0 ? dst_track_end : src_track_end;
    bool time_overlapped = end_pos_moved >= start_pos && start_pos_moved <= end_pos;
    double src_start_pos = start_pos;
    double src_end_pos = end_pos;
    double dst_start_pos = start_pos_moved;
    double dst_end_pos = end_pos_moved;

    if (duplicate) {
      first_track = dst_track_id;
      last_track = dst_track_end;
    }

    if (time_overlapped && src_start_pos > dst_start_pos) {
      std::swap(src_start_pos, dst_start_pos);
      std::swap(src_end_pos, dst_end_pos);
    }

    // bool overlapped_pos;
    // Overlapped tracks case
    for (int32_t i = first_track; i < last_track; i++) {
      Track* track = Engine2::tracks[i];

      if (duplicate) {
        if (auto span = track->query_clip_by_range2(dst_start_pos, dst_end_pos)) {
          clear_track_region(track, i, dst_start_pos, dst_end_pos, span);
        }
        continue;
      }

      bool is_dst_track = i >= dst_track_id && i < dst_track_end;
      bool is_src_track = i >= src_track_id && i < src_track_end;

      if (is_src_track && is_dst_track) {
        if (time_overlapped) {
          if (auto span = track->query_clip_by_range2(src_start_pos, dst_end_pos)) {
            clear_track_region(track, i, src_start_pos, dst_end_pos, span);
          }
        } else {
          const ClipSpan& src_span = clip_spans[i - src_track_id];
          ClipSpan dst_span = track->query_clip_by_range2(dst_start_pos, dst_end_pos);
          if (dst_span) {
            Clip* last_partially_truncated_clip = clear_track_region(track, i, src_start_pos, src_end_pos, src_span);
            clear_track_region(track, i, dst_start_pos, dst_end_pos, dst_span, last_partially_truncated_clip);
          } else {
            if (src_span.contains_clip) {
              clear_track_region(track, i, start_pos, end_pos, src_span);
            }
          }
        }
      } else if (is_src_track) {
        const ClipSpan& clip_span = clip_spans[i - src_track_id];
        if (clip_span.contains_clip) {
          clear_track_region(track, i, start_pos, end_pos, clip_span);
        }
      } else if (is_dst_track) {
        if (auto clip_span = track->query_clip_by_range2(dst_start_pos, dst_end_pos)) {
          clear_track_region(track, i, dst_start_pos, dst_end_pos, clip_span);
        }
      }
    }

    if (!truncated_clips.empty()) {
      for (auto [track_index, clip] : truncated_clips) {
        Track* track = Engine2::tracks[track_index];
        track->clips.push_back(clip);
      }
    }
  } else {
    if (!duplicate) {
      for (int32_t i = 0; i < clip_spans.size(); i++) {
        int32_t track_id = src_track_id + i;
        Track* track = Engine2::tracks[track_id];
        const ClipSpan& clip_span = clip_spans[i];
        if (clip_span.contains_clip) {
          clear_track_region(track, track_id, start_pos, end_pos, clip_span);
        }
      }
    }

    for (int32_t i = 0; i < clip_spans.size(); i++) {
      int32_t track_id = dst_track_id + i;
      Track* track = Engine2::tracks[track_id];
      if (auto clip_span = track->query_clip_by_range2(start_pos_moved, end_pos_moved)) {
        clear_track_region(track, track_id, start_pos_moved, end_pos_moved, clip_span);
      }
    }

    if (!truncated_clips.empty()) {
      for (auto [track_index, clip] : truncated_clips) {
        Track* track = Engine2::tracks[track_index];
        track->clips.push_back(clip);
      }
    }
  }

  // 2. Relocate clips
  for (int32_t i = 0; i < num_tracks; i++) {
    const ClipSpan& clip_span = clip_spans[i];
    uint32_t num_clips = (clip_span.last - clip_span.first) + 1;
    uint32_t src_index = src_track_id + i;
    uint32_t dst_index = dst_track_id + i;
    Track* src_track = Engine2::tracks[src_index];
    Track* dst_track = Engine2::tracks[dst_index];

    if (clip_span.contains_clip) {
      double min_move = 0.0;
      dst_track->clips.expand_capacity(num_clips);
      added_clips.expand_capacity(num_clips);

      for (uint32_t i = clip_span.first; i <= clip_span.last; i++) {
        bool right_side_partially_selected = clip_span.right_side_partially_selected(i);
        bool left_side_partially_selected = clip_span.left_side_partially_selected(i);
        Clip* clip = src_track->clips[i];
        double new_min_time;
        double new_max_time;
        double new_start_ofs;

        if (right_side_partially_selected && left_side_partially_selected) {
          const double shift_ofs = clip_span.first_offset;
          const double min_time = clip->min_time + shift_ofs;
          const double length = (clip->max_time - min_time) + clip_span.last_offset;
          new_min_time = math::max(min_time + relative_move_ofs, min_move);
          new_max_time = new_min_time + length;
          new_start_ofs = shift_clip_content(clip, -shift_ofs, beat_duration);
        } else if (right_side_partially_selected) {
          const double shift_ofs = clip_span.first_offset;
          const double min_time = clip->min_time + shift_ofs;
          new_min_time = math::max(min_time + relative_move_ofs, min_move);
          new_max_time = new_min_time + (clip->max_time - min_time);
          new_start_ofs = shift_clip_content(clip, -shift_ofs, beat_duration);
          min_move = new_max_time;
        } else if (left_side_partially_selected) {
          const auto [min_time, max_time] = calc_move_clip(clip, relative_move_ofs, min_move);
          new_min_time = min_time;
          new_max_time = max_time + clip_span.last_offset;
          new_start_ofs = clip->start_offset;
        } else {
          const auto [min_time, max_time] = calc_move_clip(clip, relative_move_ofs, min_move);
          new_min_time = min_time;
          new_max_time = max_time;
          new_start_ofs = clip->start_offset;
          min_move = new_max_time;
        }

        Clip* new_clip = dst_track->allocate_clip();
        assert(new_clip);
        new (new_clip) Clip(*clip);
        new_clip->min_time = new_min_time;
        new_clip->max_time = new_max_time;
        new_clip->start_offset = new_start_ofs;
        dst_track->clips.push_back(new_clip);
        added_clips.emplace_back(dst_index, 0, new_clip);
      }
    }
  }

  if (track_overlapped) {
    int32_t begin_track = dst_track_relative_ofs >= 0 ? src_track_id : dst_track_id;
    int32_t end_track = dst_track_relative_ofs >= 0 ? dst_track_end : src_track_end;

    for (int32_t i = begin_track; i < end_track; i++) {
      Track* track = Engine2::tracks[i];
      modified_tracks.push_back(i);
      Engine2::update_track_state(track);
    }
  } else {
    for (uint32_t i = src_track_id; i < src_track_end; i++) {
      Track* track = Engine2::tracks[i];
      modified_tracks.push_back(i);
      Engine2::update_track_state(track);
    }

    for (uint32_t i = dst_track_id; i < dst_track_end; i++) {
      Track* track = Engine2::tracks[i];
      modified_tracks.push_back(i);
      Engine2::update_track_state(track);
    }
  }

  Engine2::end_edit();
  resolve_id_for_added_clips();

  return true;
}

void CmdMoveClips::undo() {
  Engine2::begin_edit();
  CmdClip::restore_clip_backups();
  Engine2::end_edit();
}

//

bool CmdDeleteClips::execute() {
  if (clip_spans.size() == 0)
    return false;

  double beat_duration = Engine2::get_beat_duration();
  Engine2::begin_edit();
  for (int32_t i = first_track; const auto& clip_span : clip_spans) {
    Track* track = Engine2::tracks[i];
    delete_region(clip_span, track, i, start_pos, end_pos, beat_duration);
    Engine2::update_track_state(track);
    i++;
  }
  Engine2::end_edit();
  resolve_id_for_added_clips();

  return true;
}

void CmdDeleteClips::undo() {
  Engine2::begin_edit();
  CmdClip::restore_clip_backups();
  Engine2::end_edit();
}

}  // namespace wb