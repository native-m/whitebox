#include "command.h"

#include "engine/clip_edit.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {

void TrackHistory::backup(TrackEditResult&& edit_result) {
  deleted_clips = std::move(edit_result.deleted_clips);
  added_clips.reserve(edit_result.added_clips.size());
  modified_clips.resize(edit_result.modified_clips.size());
  for (uint32_t i = 0; i < (uint32_t)edit_result.added_clips.size(); i++)
    added_clips.push_back(*edit_result.added_clips[i]);
  for (uint32_t i = 0; i < (uint32_t)modified_clips.size(); i++)
    modified_clips[i] = edit_result.modified_clips[i]->id;
}

void TrackHistory::undo(Track* track) {
  Vector<Clip*> new_cliplist;
  for (auto clip : track->clips) {
    bool should_skip = false;

    for (auto id : modified_clips) {
      if (id == clip->id) {
        should_skip = true;
        break;
      }
    }
    if (should_skip) {
      track->destroy_clip(track->clips[clip->id]);
      continue;
    }

    for (const auto& added_clip : added_clips) {
      if (added_clip.id == clip->id) {
        should_skip = true;
        break;
      }
    }
    if (should_skip) {
      track->destroy_clip(track->clips[clip->id]);
      continue;
    }

    new_cliplist.push_back(clip);
  }
  // Restore deleted clips
  for (auto& clip : deleted_clips) {
    auto restored_clip = track->allocate_clip();
    new (restored_clip) Clip(clip);
    new_cliplist.push_back(restored_clip);
  }
  track->clips = std::move(new_cliplist);
}

//

bool TrackAddCmd::execute() {
  Track* track = g_engine.add_track("New track");
  track->color = color;
  track_id = g_engine.tracks.size() - 1;
  return true;
}

void TrackAddCmd::undo() {
  g_engine.delete_track(track_id);
}

//

bool TrackMoveCmd::execute() {
  g_engine.move_track(src_slot, dst_slot);
  return true;
}

void TrackMoveCmd::undo() {
  g_engine.move_track(dst_slot, src_slot);
}

//

bool wb::ClipAddFromFileCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  auto result = g_engine.add_clip_from_file(track, file, cursor_pos);
  assert(result.added_clips.size() && "Cannot create clip");
  history.backup(std::move(result));
  return true;
}

void ClipAddFromFileCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  std::unique_lock edit_lock(g_engine.editor_lock);
  history.undo(track);
  track->update_clip_ordering();
  track->reset_playback_state(g_engine.playhead, true);
}

//

bool ClipRenameCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  clip->name = new_name;
  return true;
}

void ClipRenameCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  clip->name = old_name;
}

//

bool ClipChangeColorCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  clip->color = new_color;
  return true;
}

void ClipChangeColorCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  clip->color = old_color;
}

//

bool ClipMoveCmd::execute() {
  Track* src_track = g_engine.tracks[src_track_id];
  Clip* clip = src_track->clips[clip_id];
  if (src_track_id == dst_track_id) {
    src_track_history.backup(g_engine.move_clip(src_track, clip, relative_pos));
  } else {
    Track* dst_track = g_engine.tracks[dst_track_id];
    double new_pos = std::max(clip->min_time + relative_pos, 0.0);
    double length = clip->max_time - clip->min_time;
    dst_track_history.backup(g_engine.duplicate_clip(dst_track, clip, new_pos, new_pos + length));
    src_track_history.backup(g_engine.delete_clip(src_track, clip));
  }
  return true;
}

void ClipMoveCmd::undo() {
  Track* src_track = g_engine.tracks[src_track_id];
  std::unique_lock edit_lock(g_engine.editor_lock);
  if (src_track_id == dst_track_id) {
    src_track_history.undo(src_track);
    src_track->update_clip_ordering();
    src_track->reset_playback_state(g_engine.playhead, true);
  } else {
    Track* dst_track = g_engine.tracks[dst_track_id];
    src_track_history.undo(src_track);
    dst_track_history.undo(dst_track);
    src_track->update_clip_ordering();
    src_track->reset_playback_state(g_engine.playhead, true);
    dst_track->update_clip_ordering();
    dst_track->reset_playback_state(g_engine.playhead, true);
  }
}

//

bool ClipShiftCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  g_engine.edit_lock();
  clip->start_offset = shift_clip_content(clip, relative_pos, last_beat_duration);
  clip->start_offset_changed = true;
  g_engine.edit_unlock();
  return true;
}

void ClipShiftCmd::undo() {
  double beat_duration = g_engine.get_beat_duration();
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  g_engine.edit_lock();
  clip->start_offset = shift_clip_content(clip, -relative_pos, last_beat_duration);
  clip->start_offset_changed = true;
  g_engine.edit_unlock();
}

//

bool ClipResizeCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  double resize_limit = left_side ? clip->max_time : clip->min_time;
  auto result = g_engine.resize_clip(track, clip, relative_pos, resize_limit, min_length, left_side, shift);
  history.backup(std::move(result));
  return true;
}

void ClipResizeCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  std::unique_lock editor_lock(g_engine.editor_lock);
  history.undo(track);
  track->update_clip_ordering();
  track->reset_playback_state(g_engine.playhead, true);
}

//

bool ClipDuplicateCmd::execute() {
  Track* src_track = g_engine.tracks[src_track_id];
  Clip* clip = src_track->clips[clip_id];
  const double min_time = math::max(clip->min_time + relative_pos, 0.0);
  const double length = clip->max_time - clip->min_time;
  const double max_time = min_time + length;
  if (src_track_id == dst_track_id) {
    track_history.backup(g_engine.duplicate_clip(src_track, clip, min_time, max_time));
  } else {
    Track* dst_track = g_engine.tracks[dst_track_id];
    track_history.backup(g_engine.duplicate_clip(dst_track, clip, min_time, max_time));
  }
  return true;
}

void ClipDuplicateCmd::undo() {
  Track* track = g_engine.tracks[src_track_id];
  std::unique_lock editor_lock(g_engine.editor_lock);
  track_history.undo(track);
  track->update_clip_ordering();
  track->reset_playback_state(g_engine.playhead, true);
}

//

bool ClipDeleteCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  // TODO: backup the clip directly
  history.backup(g_engine.delete_clip(track, clip));
  return true;
}

void ClipDeleteCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  std::unique_lock editor_lock(g_engine.editor_lock);
  history.undo(track);
  track->update_clip_ordering();
  track->reset_playback_state(g_engine.playhead, true);
}

//

bool ClipDeleteRegionCmd::execute() {
  uint32_t first_track = first_track_id;
  uint32_t last_track = last_track_id;
  if (last_track < first_track)
    std::swap(first_track, last_track);

  std::unique_lock editor_lock(g_engine.editor_lock);
  for (uint32_t i = first_track; i <= last_track; i++) {
    Track* track = g_engine.tracks[i];
    auto result = g_engine.delete_region(track, min_time, max_time);
    auto& history = histories.emplace_back();
    history.backup(std::move(result));
  }
  return true;
}

void ClipDeleteRegionCmd::undo() {
  uint32_t first_track = first_track_id;
  uint32_t last_track = last_track_id;
  if (last_track < first_track)
    std::swap(first_track, last_track);

  uint32_t range = (last_track - first_track) + 1;
  std::unique_lock editor_lock(g_engine.editor_lock);
  for (uint32_t i = 0; i < range; i++) {
    Track* track = g_engine.tracks[i + first_track];
    histories[i].undo(track);
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
  }
}

//

bool ClipAdjustGainCmd::execute() {
  Track* track = g_engine.tracks[track_id];
  g_engine.set_clip_gain(track, clip_id, gain_after);
  return true;
}

void ClipAdjustGainCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  g_engine.set_clip_gain(track, clip_id, gain_before);
}

//

void ClipCmd::backup(MultiEditResult&& edit_result) {
  deleted_clips = std::move(edit_result.deleted_clips);
  added_clips.resize(edit_result.added_clips.size());
  modified_clips.resize(edit_result.modified_clips.size());
  for (uint32_t i = 0; i < edit_result.added_clips.size(); i++) {
    auto& added_clip = added_clips[i];
    const auto& added_clip_result = edit_result.added_clips[i];
    added_clip.first = added_clip_result.first;
    added_clip.second = added_clip_result.second->id;
  }
  for (uint32_t i = 0; i < edit_result.modified_clips.size(); i++) {
    auto& modified_clip = modified_clips[i];
    const auto& modified_clip_result = edit_result.modified_clips[i];
    modified_clip.first = modified_clip_result.first;
    modified_clip.second = modified_clip_result.second->id;
  }
}

void ClipCmd::undo(uint32_t begin_track, uint32_t end_track) {
  double playback_pos = g_engine.playhead;
  for (uint32_t i = begin_track; i < end_track; i++) {
    Track* track = g_engine.tracks[i];
    Vector<Clip*> new_cliplist;

    for (auto clip : track->clips) {
      bool should_delete = false;

      for (const auto& modified_clip : modified_clips) {
        if (modified_clip.first == i && modified_clip.second == clip->id) {
          should_delete = true;
          break;
        }
      }

      for (const auto& added_clip : added_clips) {
        if (added_clip.first == i && added_clip.second == clip->id) {
          should_delete = true;
          break;
        }
      }

      if (should_delete) {
        track->destroy_clip(clip);
        continue;
      }

      new_cliplist.push_back(clip);
    }

    for (auto& deleted_clip : deleted_clips) {
      if (deleted_clip.first != i) {
        continue;
      }
      auto restored_clip = track->allocate_clip();
      new (restored_clip) Clip(deleted_clip.second);
      new_cliplist.push_back(restored_clip);
    }

    track->clips = std::move(new_cliplist);
    track->update_clip_ordering();
    track->reset_playback_state(playback_pos, true);
  }
}

void ClipCmd::clean_edit_result() {
  deleted_clips.clear();
  added_clips.clear();
  modified_clips.clear();
}

//

bool CreateMidiClipCmd::execute() {
  backup(g_engine.create_midi_clips(selected_track_regions, first_track, min_pos, max_pos));
  return true;
}

void CreateMidiClipCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  ClipCmd::undo(first_track, first_track + selected_track_regions.size());
  clean_edit_result();
}

//

bool ClipMoveCmd2::execute() {
  backup(g_engine.move_or_duplicate_region(
      selected_track_regions, src_track_idx, dst_track_relative_idx, min_pos, max_pos, relative_move_pos, duplicate));
  return true;
}

void ClipMoveCmd2::undo() {
  uint32_t num_selected_tracks = (uint32_t)selected_track_regions.size();
  uint32_t dst_track_idx = (int32_t)src_track_idx + dst_track_relative_idx;
  uint32_t src_track_end = src_track_idx + num_selected_tracks;
  uint32_t dst_track_end = dst_track_idx + num_selected_tracks;
  g_engine.edit_lock();

  if (dst_track_end > src_track_idx && dst_track_idx < src_track_end) {
    int32_t begin_track = (int32_t)(dst_track_relative_idx >= 0 ? src_track_idx : dst_track_idx);
    int32_t end_track = (int32_t)(dst_track_relative_idx >= 0 ? dst_track_end : src_track_end);
    ClipCmd::undo(begin_track, end_track);
  } else {
    ClipCmd::undo(src_track_idx, src_track_end);
    ClipCmd::undo(dst_track_idx, dst_track_end);
  }

  clean_edit_result();
  g_engine.edit_unlock();
}

//

bool ClipResizeCmd2::execute() {
  backup(g_engine.resize_clips(
      track_clip, first_track, relative_pos, resize_limit, min_length, min_resize_pos, left_side, shift));
  return true;
}

void ClipResizeCmd2::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  ClipCmd::undo(first_track, first_track + track_clip.size());
}

//

bool ClipDeleteCmd2::execute() {
  backup(g_engine.delete_region(selected_track_regions, first_track, min_pos, max_pos));
  return true;
}

void ClipDeleteCmd2::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  ClipCmd::undo(first_track, first_track + selected_track_regions.size());
}

//

void MidiCmd::backup(MidiEditResult&& edit_result) {
  modified_notes = std::move(edit_result.modified_notes);
  deleted_notes = std::move(edit_result.deleted_notes);
}

void MidiCmd::undo(uint32_t track_id, uint32_t clip_id, uint32_t channel) {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  MidiAsset* asset = clip->midi.asset;
  MidiNoteBuffer& note_sequence = asset->data.note_sequence;
  MidiNoteMetadataPool& metadata_pool = asset->data.note_metadata_pool;
  uint32_t num_erased = 0;

  // Delete modified notes
  MidiNoteBuffer new_sequence;
  new_sequence.reserve(note_sequence.size());

  for (uint32_t note_id = 0; const auto& note : note_sequence) {
    bool skip = false;
    for (uint32_t id : modified_notes) {
      if (id == note_id) {
        skip = true;
      }
    }
    if (!skip) {
      new_sequence.push_back(note);
    }
    note_id++;
  }

  note_sequence = std::move(new_sequence);

  // Restore deleted notes
  MidiNote* restored_notes = note_sequence.append(deleted_notes.begin(), deleted_notes.end());
  asset->data.create_metadata(restored_notes, deleted_notes.size());
  asset->data.update_channel(channel);
}

//

bool MidiAddNoteCmd::execute() {
  backup(g_engine.add_note(track_id, clip_id, min_time, max_time, velocity, note_key, channel));
  return true;
}

void MidiAddNoteCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  MidiCmd::undo(track_id, clip_id, channel);
  // g_engine.delete_note()
}

//

bool MidiPaintNotesCmd::execute() {
  backup(g_engine.add_note(track_id, clip_id, channel, notes));
  return true;
}

void MidiPaintNotesCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  MidiCmd::undo(track_id, clip_id, channel);
}

//

bool MidiMoveNoteCmd::execute() {
  if (move_selected) {
    backup(g_engine.move_selected_note(track_id, clip_id, relative_key_pos, relative_pos));
  } else {
    backup(g_engine.move_note(track_id, clip_id, note_id, relative_key_pos, relative_pos));
  }
  return true;
}

void MidiMoveNoteCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  MidiCmd::undo(track_id, clip_id, 0);
}

//

bool MidiSliceNoteCmd::execute() {
  if (auto result = g_engine.slice_note(track_id, clip_id, pos, velocity, note_key, channel)) {
    backup(std::move(result.value()));
    return true;
  }
  return false;
}

void MidiSliceNoteCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  MidiCmd::undo(track_id, clip_id, channel);
}

//

bool MidiSelectNoteCmd::execute() {
  result = g_engine.select_note(track_id, clip_id, min_pos, max_pos, min_key, max_key);
  return !result.selected.empty() || !result.deselected.empty();
}

void MidiSelectNoteCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  MidiData* data = clip->get_midi_data();
  MidiNoteBuffer& note_seq = data->note_sequence;
  
  for (uint32_t id : result.selected) {
    MidiNote& note = note_seq[id];
    note.flags &= ~MidiNoteFlags::Selected;
    data->num_selected--;
  }

  for (uint32_t id : result.deselected) {
    MidiNote& note = note_seq[id];
    note.flags |= MidiNoteFlags::Selected;
    data->num_selected++;
  }
}

//

bool MidiSelectOrDeselectNotesCmd::execute() {
  result = g_engine.select_or_deselect_notes(track_id, clip_id, should_select);
  return !result.selected.empty() || !result.deselected.empty();
}

void MidiSelectOrDeselectNotesCmd::undo() {
  Track* track = g_engine.tracks[track_id];
  Clip* clip = track->clips[clip_id];
  MidiData* data = clip->get_midi_data();
  MidiNoteBuffer& note_seq = data->note_sequence;

  for (uint32_t id : result.selected) {
    MidiNote& note = note_seq[id];
    note.flags &= ~MidiNoteFlags::Selected;
    data->num_selected--;
  }

  for (uint32_t id : result.deselected) {
    MidiNote& note = note_seq[id];
    note.flags |= MidiNoteFlags::Selected;
    data->num_selected++;
  }
}

//

bool MidiAppendNoteSelectionCmd::execute() {
  g_engine.append_note_selection(track_id, clip_id, select_or_deselect, selected_note_ids);
  return true;
}

void MidiAppendNoteSelectionCmd::undo() {
  g_engine.append_note_selection(track_id, clip_id, !select_or_deselect, selected_note_ids);
}

//

bool MidiDeleteNoteCmd::execute() {
  backup(g_engine.delete_marked_notes(track_id, clip_id, selected));
  return !deleted_notes.empty();
}

void MidiDeleteNoteCmd::undo() {
  std::unique_lock lock(g_engine.editor_lock);
  MidiCmd::undo(track_id, clip_id, 0);
}

}  // namespace wb