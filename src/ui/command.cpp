#include "command.h"
#include "engine/clip_edit.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {

void ClipMoveCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    if (track_id == target_track_id) {
        track->move_clip(clip, relative_pos, beat_duration);
        clip_id = clip->id;
    } else {
        Track* target_track = g_engine.tracks[target_track_id];
        double new_pos = std::max(clip->min_time + relative_pos, 0.0);
        double length = clip->max_time - clip->min_time;
        Clip* new_clip =
            target_track->duplicate_clip(clip, new_pos, new_pos + length, beat_duration);
        clip_id = new_clip->id;
        g_engine.delete_clip(track, clip);
    }
    g_engine.edit_unlock();
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
        Clip* new_clip =
            target_track->duplicate_clip(clip, new_pos, new_pos + length, beat_duration);
        clip_id = new_clip->id;
        g_engine.delete_clip(track, clip);
    }
    g_engine.edit_unlock();
}

void ClipShiftCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    double rel_offset = calc_shift_clip(clip, relative_pos);
    clip->relative_start_time = rel_offset;
    if (clip->type == ClipType::Audio) {
        SampleAsset* asset = clip->audio.asset;
        clip->audio.sample_offset =
            beat_to_samples(rel_offset, (double)asset->sample_instance.sample_rate, beat_duration);
    }
    g_engine.edit_unlock();
}

void ClipShiftCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    double start_offset = calc_shift_clip(clip, -relative_pos);
    clip->relative_start_time = start_offset;
    if (clip->type == ClipType::Audio) {
        SampleAsset* asset = clip->audio.asset;
        clip->audio.sample_offset =
            beat_to_samples(start_offset, (double)asset->sample_instance.sample_rate, beat_duration);
    }
    g_engine.edit_unlock();
}

void ClipResizeCmd::execute() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    track->resize_clip(clip, relative_pos, min_length, beat_duration, left_side);
    g_engine.edit_unlock();
    clip_id = clip->id;
}

void ClipResizeCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    track->resize_clip(clip, -relative_pos, min_length, beat_duration, left_side);
    g_engine.edit_unlock();
    clip_id = clip->id;
}

} // namespace wb