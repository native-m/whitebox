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
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
}

void ClipMoveCmd::execute() {
    Track* src_track = g_engine.tracks[src_track_id];
    Clip* clip = src_track->clips[clip_id];
    if (src_track_id == dst_track_id) {
        src_track_history.backup(g_engine.move_clip(src_track, clip, relative_pos));
    } else {
        Track* dst_track = g_engine.tracks[dst_track_id];
        double beat_duration = g_engine.get_beat_duration();
        double new_pos = std::max(clip->min_time + relative_pos, 0.0);
        double length = clip->max_time - clip->min_time;
        dst_track_history.backup(g_engine.duplicate_clip(dst_track, clip, new_pos, new_pos + length));
        src_track_history.backup(g_engine.delete_clip(src_track, clip));
    }
}

void ClipMoveCmd::undo() {
    Track* src_track = g_engine.tracks[src_track_id];
    std::unique_lock edit_lock(g_engine.editor_lock);
    std::unique_lock delete_lock(g_engine.delete_lock);
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
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    auto result = g_engine.resize_clip(track, clip, relative_pos, min_length, left_side);
    history.backup(std::move(result));
}

void ClipResizeCmd::undo() {
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    std::unique_lock editor_lock(g_engine.editor_lock);
    std::unique_lock delete_lock(g_engine.delete_lock);
    history.undo(track);
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
}

void ClipDeleteCmd::execute() {
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    // TODO: backup the clip directly
    history.backup(g_engine.delete_clip(track, clip));
}

void ClipDeleteCmd::undo() {
    Track* track = g_engine.tracks[track_id];
    std::unique_lock editor_lock(g_engine.editor_lock);
    history.undo(track);
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
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