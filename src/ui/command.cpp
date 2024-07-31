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
    track->move_clip(clip, relative_pos, beat_duration);
    g_engine.edit_unlock();
    clip_id = clip->id;
}

void ClipMoveCmd::undo() {
    double beat_duration = g_engine.get_beat_duration();
    Track* track = g_engine.tracks[track_id];
    Clip* clip = track->clips[clip_id];
    g_engine.edit_lock();
    track->move_clip(clip, -relative_pos, beat_duration);
    g_engine.edit_unlock();
    clip_id = clip->id;
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
    double rel_offset = calc_shift_clip(clip, -relative_pos);
    clip->relative_start_time = rel_offset;
    if (clip->type == ClipType::Audio) {
        SampleAsset* asset = clip->audio.asset;
        clip->audio.sample_offset =
            beat_to_samples(rel_offset, (double)asset->sample_instance.sample_rate, beat_duration);
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