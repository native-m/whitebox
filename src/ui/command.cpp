#include "command.h"
#include "engine/clip_edit.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {

ClipHistory::ClipHistory() : audio() {
}

ClipHistory::ClipHistory(const Clip& clip, uint32_t track_id) :
    clip_id(clip.id),
    track_id(track_id),
    type(clip.type),
    name(clip.name),
    color(clip.color),
    min_time(clip.min_time),
    max_time(clip.max_time),
    start_offset(clip.start_offset) {
    switch (type) {
        case ClipType::Audio:
            audio = clip.audio;
            audio.asset->keep_alive = true;
            break;
        case ClipType::Midi:
            midi = clip.midi;
            midi.asset->keep_alive = true;
            break;
        default:
            break;
    }
}

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
        track->add_audio_clip(clip_state.name, clip_state.min_time, clip_state.max_time,
                              clip_state.start_offset, clip_state.audio, beat_duration,
                              clip_state.is_active());
    } else {
        clip_state.midi.asset->add_ref();
        track->add_midi_clip(clip_state.name, clip_state.min_time, clip_state.max_time,
                             clip_state.start_offset, clip_state.midi, beat_duration,
                             clip_state.is_active());
    }
    g_engine.edit_unlock();
}

void ClipDeleteRegionCmd::execute() {
}

void ClipDeleteRegionCmd::undo() {
}

} // namespace wb