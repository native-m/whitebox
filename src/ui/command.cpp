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

void ClipAddFromFileCmd::execute() {
    Track* track = g_engine.tracks[track_id];
    auto result = g_engine.add_clip_from_file(track, file, cursor_pos);
    assert(result.added_clips.size() && "Cannot create clip");
    history.backup(std::move(result));
}

void ClipAddFromFileCmd::undo() {
    Track* track = g_engine.tracks[track_id];
    std::unique_lock edit_lock(g_engine.editor_lock);
    std::unique_lock delete_lock(g_engine.delete_lock);
    history.undo(track);
    g_engine.update_clip_ordering(track);
    track->reset_playback_state(g_engine.playhead, true);
}

void ClipMoveCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    if (track_id == target_track_id) {
        g_engine.move_clip(track, clip, relative_pos);
        clip_id = clip->id;
    } else {
        Track* target_track = g_engine.tracks[target_track_id];
        double new_pos = std::max(clip->min_time + relative_pos, 0.0);
        double length = clip->max_time - clip->min_time;
        Clip* new_clip = target_track->duplicate_clip(clip, new_pos, new_pos + length, beat_duration);
        clip_id = new_clip->id;
        g_engine.delete_clip(track, clip);
    }
}

void ClipMoveCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[target_track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    if (track_id == target_track_id) {
        track->move_clip(clip, -relative_pos, beat_duration);
        clip_id = clip->id;
    } else {
        Track* target_track = g_engine.tracks[track_id];
        double new_pos = std::max(clip->min_time - relative_pos, 0.0);
        double length = clip->max_time - clip->min_time;
        Clip* new_clip = target_track->duplicate_clip(clip, new_pos, new_pos + length, beat_duration);
        clip_id = new_clip->id;
        g_engine.delete_clip(track, clip);
    }
    g_engine.edit_unlock();
}

void ClipShiftCmd::execute() {
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    clip->start_offset = shift_clip_content(clip, relative_pos, last_beat_duration);
    clip->start_offset_changed = true;
    g_engine.edit_unlock();
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

void ClipResizeCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    track->resize_clip(clip, relative_pos, min_length, last_beat_duration, left_side);
    g_engine.edit_unlock();
    clip_id = clip->id;
}

void ClipResizeCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    track->resize_clip(clip, -relative_pos, min_length, last_beat_duration, left_side);
    g_engine.edit_unlock();
    clip_id = clip->id;
}

void ClipDeleteCmd::execute() {
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.delete_clip(track, clip);
}

void ClipDeleteCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    g_engine.edit_lock();
    if (clip_state.is_audio()) {
        clip_state.audio.asset->add_ref();
        track->add_audio_clip(clip_state.name, clip_state.min_time, clip_state.max_time, clip_state.start_offset,
                              clip_state.audio, beat_duration, clip_state.is_active());
    } else {
        clip_state.midi.asset->add_ref();
        track->add_midi_clip(clip_state.name, clip_state.min_time, clip_state.max_time, clip_state.start_offset,
                             clip_state.midi, beat_duration, clip_state.is_active());
    }
    g_engine.edit_unlock();
}

void ClipDeleteRegionCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    uint32_t first_track = first_track_id;
    uint32_t last_track = last_track_id;
    g_engine.edit_lock();
    if (last_track < first_track) {
        std::swap(first_track, last_track);
    }
    for (uint32_t i = first_track; i <= last_track; i++) {
        Track* track = g_engine.tracks[i];
        auto query_result = track->query_clip_by_range(min_time, max_time);
        if (!query_result) {
            continue;
        }
        Log::debug("first: {} {}, last: {} {}", query_result->first, query_result->first_offset, query_result->last,
                   query_result->last_offset);
        for (uint32_t j = query_result->first; j <= query_result->last; j++)
            old_clips.emplace_back(*track->clips[j], i);
        TrackEditResult result = g_engine.reserve_track_region(track, query_result->first, query_result->last, min_time,
                                                              max_time, false, nullptr);
    }
    g_engine.edit_unlock();
}

void ClipDeleteRegionCmd::undo() {
}

} // namespace wb