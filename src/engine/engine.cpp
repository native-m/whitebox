#include "engine.h"
#include "clip_edit.h"
#include "core/debug.h"
#include "core/core_math.h"
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

void Engine::set_buffer_size(uint32_t channels, uint32_t size) {
    mixing_buffer.resize(size);
    mixing_buffer.resize_channel(channels);
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
        track->reset_playback_state(playhead_start);
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
    editor_lock.unlock();
    Log::debug("-------------- Stop --------------");
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
        if (i == slot) {
            continue;
        }
        if (tracks[i]->ui_parameter_state.solo) {
            tracks[i]->ui_parameter_state.solo = false;
        }
        tracks[i]->set_mute(mute);
    }
}

Clip* Engine::add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time) {
    bool is_midi = false;
    Clip* clip = nullptr;

    if (SampleAsset* sample_asset = g_sample_table.load_from_file(path)) {
        double sample_rate = (double)sample_asset->sample_instance.sample_rate;
        double clip_length =
            samples_to_beat(sample_asset->sample_instance.count, sample_rate, beat_duration);
        double max_time = min_time + math::uround(clip_length * ppq) / ppq;
        clip = track->add_audio_clip(path.filename().string(), min_time, max_time, 0.0,
                                     {.asset = sample_asset}, beat_duration);
        if (!clip) {
            sample_asset->release();
            return nullptr;
        }

        return clip;
    }

    if (MidiAsset* midi_asset = g_midi_table.load_from_file(path)) {
        clip = track->add_midi_clip("", min_time, min_time + midi_asset->data.max_length, 0.0,
                                    {.asset = midi_asset}, beat_duration);
        if (!clip) {
            midi_asset->release();
            return nullptr;
        }

        return clip;
    }

    return nullptr;
}

void Engine::delete_clip(Track* track, Clip* clip) {
    track->delete_clip(clip->id);
    has_deleted_clips.store(true, std::memory_order_release);
}

void Engine::trim_track_by_range(Track* track, uint32_t first_clip, uint32_t last_clip, double min,
                                 double max, bool dont_sort) {
    Vector<Clip*>& clips = track->clips;
    double current_beat_duration = beat_duration.load(std::memory_order_relaxed);

    if (first_clip == last_clip) {
        Clip* clip = clips[first_clip];
        if (min > clip->min_time && max < clip->max_time) {
            // Split clip into two parts
            Clip* new_clip = (Clip*)track->clip_allocator.allocate();
            if (!new_clip)
                return;
            new (new_clip) Clip(*clip);
            new_clip->min_time = max;
            new_clip->start_offset =
                shift_clip_content(new_clip, clip->min_time - max, current_beat_duration);
            clip->max_time = min;
            clips.push_back(new_clip);
            std::sort(clips.begin(), clips.end(),
                      [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });
            for (uint32_t i = clip->id; i < (uint32_t)clips.size(); i++) {
                clips[i]->id = i;
            }
        } else if (min > clip->min_time) {
            clip->max_time = min;
        } else if (max < clip->max_time) {
            clip->start_offset =
                shift_clip_content(clip, clip->min_time - max, current_beat_duration);
            clip->min_time = max;
        } else {
            delete_clip(track, clip);
        }
        return;
    }

    Clip* first = clips[first_clip];
    Clip* last = clips[last_clip];

    if (min > first->min_time) {
        first->max_time = min;
        first_clip++;
    }

    if (max < last->max_time) {
        last->start_offset = shift_clip_content(last, last->min_time - max, current_beat_duration);
        last->min_time = max;
        last_clip--;
    }

    for (uint32_t i = first_clip; i <= last_clip; i++) {
        clips[i]->mark_deleted();
    }

    has_deleted_clips.store(true, std::memory_order_release);
}

double Engine::get_song_length() const {
    return 0.0;
}

void Engine::process(AudioBuffer<float>& output_buffer, double sample_rate) {
    double buffer_duration = (double)output_buffer.n_samples / sample_rate;
    bool currently_playing = playing.load(std::memory_order_relaxed);

    editor_lock.lock();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        track->audio_event_buffer.resize(0);
        track->midi_event_list.clear();
        if (track->midi_voice_state.voice_mask != 0 && !currently_playing) {
            track->stop_midi_notes(0, playhead_ui.load(std::memory_order_relaxed));
        }
    }

    if (currently_playing) {
        double inv_ppq = 1.0 / ppq;
        double current_beat_duration = beat_duration.load(std::memory_order_relaxed);

        // Record a sequence of events from track clips.
        while (playhead < playhead_ui.load(std::memory_order_relaxed) +
                              (buffer_duration / current_beat_duration)) {
            double position = std::round(playhead * ppq) * inv_ppq;
            uint32_t buffer_offset =
                (uint32_t)((uint64_t)sample_position % output_buffer.n_samples);
            for (auto track : tracks) {
                track->process_event(buffer_offset, position, current_beat_duration, sample_rate,
                                     ppq, inv_ppq);
            }
            playhead += inv_ppq;
            sample_position += beat_to_samples(inv_ppq, sample_rate, current_beat_duration);
            current_beat_duration = beat_duration.load(std::memory_order_relaxed);
        }

        playhead_ui.store(playhead_ui.load(std::memory_order_relaxed) +
                              (buffer_duration / current_beat_duration),
                          std::memory_order_release);
    }

    output_buffer.clear();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        mixing_buffer.clear();
        track->process(mixing_buffer, sample_rate, currently_playing);
        output_buffer.mix(mixing_buffer);
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