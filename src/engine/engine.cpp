#include "engine.h"
#include "clip_edit.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "track.h"
#include <numbers>

namespace wb {

Engine::~Engine() {
}

void Engine::set_bpm(double bpm) {
    double new_beat_duration = 60.0 / bpm;
    beat_duration.store(new_beat_duration, std::memory_order_release);
    for (auto& listener : on_bpm_change_listener) {
        listener(new_beat_duration, bpm);
    }
}

void Engine::set_playhead_position(double beat_position) {
    // TODO: Allow playhead dragging.
    // assert(!playing && "Dragging playhead while playing is not allowed yet!");
    editor_lock.lock();
    playhead_start = beat_position;
    playhead = playhead_start;
    playhead_ui = playhead_start;
    playhead_updated.store(true, std::memory_order_release);
    editor_lock.unlock();
}

void Engine::set_audio_channel_config(uint32_t input_channels, uint32_t output_channels, uint32_t buffer_size) {
    num_input_channels = input_channels;
    num_output_channels = output_channels;
    audio_buffer_size = buffer_size;
    mixing_buffer.resize(buffer_size);
    mixing_buffer.resize_channel(output_channels);
}

void Engine::clear_all() {
    for (auto track : tracks) {
        delete track;
    }
    tracks.clear();
}

void Engine::play() {
    Log::debug("-------------- Playing --------------");
    editor_lock.lock();
    for (auto track : tracks) {
        if (recording)
            track->prepare_record(playhead_start);
        track->reset_playback_state(playhead_start, false);
    }
    playhead_updated.store(false, std::memory_order_release);
    sample_position = 0;
    playing = true;
    editor_lock.unlock();
}

void Engine::stop() {
    editor_lock.lock();
    playing = false;
    playhead = playhead_start;
    playhead_ui = playhead_start;
    for (auto track : tracks) {
        track->stop();
    }
    recording = false;
    editor_lock.unlock();
    Log::debug("-------------- Stop --------------");
}

void Engine::record() {
    if (recording && playing)
        return;
    recording = true;
    play();
    Log::debug("Record");
}

void Engine::stop_record() {
    if (!recording)
        return;
    editor_lock.lock();
    for (auto track : tracks) {
        track->stop_record();
    }
    recording = false;
    editor_lock.unlock();
    Log::debug("Record stop");
}

void Engine::arm_track_recording(uint32_t slot, bool armed) {
    Track* track = tracks[slot];
    set_track_input(slot, track->input.mode, track->input.index, armed);
}

void Engine::set_track_input(uint32_t slot, TrackInputMode mode, uint32_t index, bool armed) {
    assert(slot < tracks.size());
    Track* track = tracks[slot];
    TrackInput new_input {mode, index};
    track->arm_record = armed;

    if (armed && (track->input.mode != mode || track->input.index != index)) {
        auto input = track_input_mapping.try_emplace(track->input.as_packed_u32());
        input.first->second.erase(slot);
        if (input.first->second.size() == 0)
            track_input_mapping.erase(track->input.as_packed_u32());
        if (mode != TrackInputMode::None) {
            input = track_input_mapping.try_emplace(new_input.as_packed_u32());
            input.first->second.emplace(slot);
        }
    } else {
        auto input = track_input_mapping.try_emplace(new_input.as_packed_u32());
        if (armed && mode != TrackInputMode::None)
            input.first->second.emplace(slot);
        else {
            input.first->second.erase(slot);
            if (input.first->second.size() == 0)
                track_input_mapping.erase(new_input.as_packed_u32());
        }
    }

    Log::debug("--- Input mapping ---");

    for (auto& [input, tracks] : track_input_mapping) {
        Log::debug("{}:", TrackInput::from_packed_u32(input).index);
        for (auto track : tracks)
            Log::debug("{}", track);
    }

    track->input.mode = mode;
    track->input.index = index;
}

Track* Engine::add_track(const std::string& name) {
    Track* new_track = new Track();
    new_track->name = name;
    tracks.push_back(new_track);
    return new_track;
}

void Engine::delete_track(uint32_t slot) {
    editor_lock.lock();
    delete_lock.lock();
    Track* track = tracks[slot];
    tracks.erase(tracks.begin() + slot);
    delete track;
    delete_lock.unlock();
    editor_lock.unlock();
}

void Engine::move_track(uint32_t from_slot, uint32_t to_slot) {
    if (from_slot == to_slot)
        return;

    Track* tmp = tracks[from_slot];

    if (from_slot < to_slot) {
        for (uint32_t i = from_slot; i < to_slot; i++)
            tracks[i] = tracks[i + 1];
    } else {
        for (uint32_t i = from_slot; i > to_slot; i--)
            tracks[i + 1] = tracks[i];
    }

    tracks[to_slot] = tmp;
}

void Engine::solo_track(uint32_t slot) {
    bool mute = false;
    if (tracks[slot]->ui_parameter_state.solo) {
        tracks[slot]->ui_parameter_state.solo = false;
    } else {
        tracks[slot]->ui_parameter_state.solo = true;
        tracks[slot]->set_mute(false);
        mute = true;
    }

    for (uint32_t i = 0; i < tracks.size(); i++) {
        if (i == slot)
            continue;
        if (tracks[i]->ui_parameter_state.solo)
            tracks[i]->ui_parameter_state.solo = false;
        tracks[i]->set_mute(mute);
    }
}

TrackEditResult Engine::add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time) {
    bool is_midi = false;
    Clip* clip = nullptr;

    if (SampleAsset* sample_asset = g_sample_table.load_from_file(path)) {
        double sample_rate = (double)sample_asset->sample_instance.sample_rate;
        double clip_length = samples_to_beat(sample_asset->sample_instance.count, sample_rate, beat_duration);
        double max_time = min_time + math::uround(clip_length * ppq) / ppq;
        return add_audio_clip(track, path.filename().string(), min_time, max_time, 0.0, {.asset = sample_asset});
    }

    if (MidiAsset* midi_asset = g_midi_table.load_from_file(path)) {
        return add_midi_clip(track, "", min_time, min_time + midi_asset->data.max_length, 0.0, {.asset = midi_asset});
    }

    return {};
}

TrackEditResult Engine::add_audio_clip(Track* track, const std::string& name, double min_time, double max_time,
                                       double start_offset, const AudioClip& clip_info, bool active) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(name, track->color, min_time, max_time);
    clip->init_as_audio_clip(clip_info);
    clip->start_offset = start_offset;
    clip->set_active(active);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::add_midi_clip(Track* track, const std::string& name, double min_time, double max_time,
                                      double start_offset, const MidiClip& clip_info, bool active) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(name, track->color, min_time, max_time);
    clip->init_as_midi_clip(clip_info);
    clip->start_offset = start_offset;
    clip->set_active(active);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::emplace_clip(Track* track, const Clip& new_clip) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    new (clip) Clip(new_clip);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::duplicate_clip(Track* track, Clip* clip_to_duplicate, double min_time, double max_time) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(*clip_to_duplicate);
    clip->min_time = min_time;
    clip->max_time = max_time;
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::move_clip(Track* track, Clip* clip, double relative_pos) {
    if (relative_pos == 0.0)
        return {};
    std::unique_lock lock(editor_lock);
    auto [min_time, max_time] = calc_move_clip(clip, relative_pos);
    auto query_result = track->query_clip_by_range(min_time, max_time);
    TrackEditResult trim_result = query_result ? reserve_track_region(track, query_result->first, query_result->last,
                                                                      min_time, max_time, true, clip)
                                               : TrackEditResult {};
    trim_result.deleted_clips.push_back(*clip); // Save previous state as deleted
    clip->min_time = min_time;
    clip->max_time = max_time;
    clip->start_offset_changed = true;
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    trim_result.added_clips.push_back(clip);
    return trim_result;
}

TrackEditResult Engine::resize_clip(Track* track, Clip* clip, double relative_pos, double min_length, bool right_side,
                                    bool shift) {
    if (relative_pos == 0.0)
        return {};
    std::unique_lock lock(editor_lock);
    auto [min_time, max_time, start_offset] =
        calc_resize_clip(clip, relative_pos, min_length, beat_duration, right_side, shift);
    auto query_result = track->query_clip_by_range(min_time, max_time);
    TrackEditResult trim_result = query_result ? reserve_track_region(track, query_result->first, query_result->last,
                                                                      min_time, max_time, true, clip)
                                               : TrackEditResult {};
    trim_result.deleted_clips.push_back(*clip);
    if (right_side) {
        clip->min_time = min_time;
        clip->start_offset = start_offset;
    } else {
        clip->max_time = max_time;
        clip->start_offset = start_offset;
    }
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    trim_result.added_clips.push_back(clip);
    return trim_result;
}

TrackEditResult Engine::delete_clip(Track* track, Clip* clip) {
    TrackEditResult result;
    result.deleted_clips.push_back(*clip);
    track->delete_clip(clip);
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    return result;
}

TrackEditResult Engine::add_to_cliplist(Track* track, Clip* clip) {
    auto& clips = track->clips;
    if (clips.size() == 0) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clip->id = 0;
        clips.push_back(clip);
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    // Add to the back
    if (auto last_clip = clips.back(); last_clip->max_time < clip->min_time) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clip->id = last_clip->id + 1;
        clips.push_back(clip);
        track->reset_playback_state(playhead, true);
        return trim_result;
    }
    // Add to the front
    if (auto first_clip = clips.front(); first_clip->min_time > clip->max_time) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clips.push_front(clip);
        for (uint32_t i = 0; i < (uint32_t)clips.size(); i++)
            clips[i]->id = i;
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    auto result = track->query_clip_by_range(clip->min_time, clip->max_time);

    // No clip found
    if (!result) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clips.push_back(clip);
        track->update_clip_ordering();
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    // Trim to reserve space for the clip
    TrackEditResult trim_result =
        reserve_track_region(track, result->first, result->last, clip->min_time, clip->max_time, true, nullptr);
    trim_result.added_clips.push_back(clip);
    clips.push_back(clip);
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);

    return trim_result;
}

TrackEditResult Engine::delete_region(Track* track, double min, double max) {
    auto query_result = track->query_clip_by_range(min, max);
    if (!query_result) {
        return {};
    }
    TrackEditResult result =
        g_engine.reserve_track_region(track, query_result->first, query_result->last, min, max, false, nullptr);
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
    return result;
}

std::optional<ClipQueryResult> Engine::query_clip_by_range(Track* track, double min, double max) const {
    return track->query_clip_by_range(min, max);
}

TrackEditResult Engine::reserve_track_region(Track* track, uint32_t first_clip, uint32_t last_clip, double min,
                                             double max, bool dont_sort, Clip* ignore_clip) {
    Vector<Clip*>& clips = track->clips;
    if (clips.size() == 0)
        return {};

    double current_beat_duration = beat_duration.load(std::memory_order_relaxed);
    Vector<Clip> deleted_clips;
    Vector<Clip*> added_clips;
    Vector<Clip*> modified_clips;

    if (first_clip == last_clip) {
        Clip* clip = clips[first_clip];
        if (clip == ignore_clip) {
            return {};
        }

        deleted_clips.push_back(*clip);
        if (min > clip->min_time && max < clip->max_time) {
            // Split clip into two parts
            Clip* new_clip = (Clip*)track->clip_allocator.allocate();
            if (!new_clip) {
                Log::error("Cannot allocate new clip");
                return {};
            }
            new (new_clip) Clip(*clip);
            new_clip->min_time = max;
            new_clip->start_offset = shift_clip_content(new_clip, clip->min_time - max, current_beat_duration);
            modified_clips.push_back(new_clip);
            clip->max_time = min;
            bool locked = editor_lock.try_lock();
            clips.push_back(new_clip);
            if (locked) {
                editor_lock.unlock();
            }
        } else if (min > clip->min_time) {
            clip->max_time = min;
        } else if (max < clip->max_time) {
            clip->start_offset = shift_clip_content(clip, clip->min_time - max, current_beat_duration);
            clip->min_time = max;
        } else {
            track->delete_clip(clip);
            return {
                .deleted_clips = std::move(deleted_clips),
            };
        }
        modified_clips.push_back(clip);
        return {
            .deleted_clips = std::move(deleted_clips),
            .added_clips = std::move(added_clips),
            .modified_clips = std::move(modified_clips),
        };
    }

    Clip* first = clips[first_clip];
    Clip* last = clips[last_clip];

    if (first != ignore_clip && min > first->min_time) {
        deleted_clips.push_back(*first);
        modified_clips.push_back(first);
        first->max_time = min;
        first_clip++;
    }

    if (last != ignore_clip && max < last->max_time) {
        deleted_clips.push_back(*last);
        modified_clips.push_back(last);
        last->start_offset = shift_clip_content(last, last->min_time - max, current_beat_duration);
        last->min_time = max;
        last_clip--;
    }

    if (first_clip <= last_clip) {
        deleted_clips.reserve((last_clip - first_clip) + 1);
        for (uint32_t i = first_clip; i <= last_clip; i++) {
            if (clips[i] != ignore_clip) {
                deleted_clips.push_back(*clips[i]);
                track->delete_clip(clips[i]);
            }
        }
    }

    return {
        .deleted_clips = std::move(deleted_clips),
        .modified_clips = std::move(modified_clips),
    };
}

double Engine::get_song_length() const {
    double max_length = std::numeric_limits<double>::min();
    for (auto track : tracks) {
        if (!track->clips.empty()) {
            Clip* clip = track->clips.back();
            max_length = math::max(max_length, clip->max_time * ppq);
        } else {
            max_length = math::max(max_length, 10000.0);
        }
    }
    return max_length;
}

void Engine::process(const AudioBuffer<float>& input_buffer, AudioBuffer<float>& output_buffer, double sample_rate) {
    double buffer_duration = (double)output_buffer.n_samples / sample_rate;
    bool currently_playing = playing.load(std::memory_order_relaxed);

    editor_lock.lock();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        track->audio_event_buffer.resize(0);
        track->midi_event_list.clear();
        if (track->midi_voice_state.has_voice() && !currently_playing) {
            track->stop_midi_notes(0, playhead);
        }
    }

    if (currently_playing) {
        double inv_ppq = 1.0 / ppq;
        double current_beat_duration = beat_duration.load(std::memory_order_relaxed);
        double buffer_duration_in_beats = buffer_duration / current_beat_duration;
        double next_playhead_pos = playhead + buffer_duration_in_beats;

        for (auto track : tracks)
            track->process_event(playhead, next_playhead_pos, sample_position, current_beat_duration,
                                 buffer_duration_in_beats, sample_rate, ppq, inv_ppq, output_buffer.n_samples);

        sample_position += beat_to_samples(buffer_duration_in_beats, sample_rate, current_beat_duration);
        playhead = next_playhead_pos;
        playhead_ui.store(playhead, std::memory_order_release);
    }

    output_buffer.clear();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        mixing_buffer.clear();
        track->process(input_buffer, mixing_buffer, sample_rate, currently_playing);
        output_buffer.mix(mixing_buffer);
    }

    // output_buffer.mix(input_buffer);

    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* channel = output_buffer.get_write_pointer(i);
        for (uint32_t j = 0; j < output_buffer.n_samples; j++) {
            if (channel[j] > 1.0) {
                channel[j] = 1.0;
            } else if (channel[j] < -1.0) {
                channel[j] = -1.0;
            }
        }
    }

    editor_lock.unlock();

    if (has_deleted_clips.load(std::memory_order_relaxed)) {
        delete_lock.lock();
        for (auto track : tracks) {
            Log::debug("Deleting pending clips for track: {} ...", (uintptr_t)track);
            track->flush_deleted_clips(playhead);
        }
        delete_lock.unlock();
        has_deleted_clips.store(false, std::memory_order_relaxed);
    }
}

Engine g_engine;

} // namespace wb