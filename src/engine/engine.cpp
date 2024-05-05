#include "engine.h"
#include "core/math.h"
#include <numbers>

namespace wb {

Engine::~Engine() {
    for (auto track : tracks) {
        delete track;
    }
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
    assert(!playing && "Dragging playhead while playing is not allowed yet!");
    playhead_start = beat_position;
    playhead = playhead_start;
    playhead_ui = playhead_start;
    playhead_updated.store(true, std::memory_order_release);
}

void Engine::play() {
    for (auto track : tracks) {
        track->prepare_play(playhead_start);
    }

    playhead_updated.store(false, std::memory_order_release);
    sample_position = 0;
    playing = true;
}

void Engine::stop() {
    playing = false;
    playhead = playhead_start;
    playhead_ui = playhead_start;
}

Track* Engine::add_track(const std::string& name) {
    Track* new_track = new Track();
    new_track->name = name;
    tracks.push_back(new_track);
    return new_track;
}

void Engine::delete_track(uint32_t slot) {
    tracks.erase(tracks.begin() + slot);
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

Clip* Engine::add_audio_clip_from_file(Track* track, const std::filesystem::path& path,
                                       double min_time) {
    SampleAsset* asset = g_sample_table.load_sample_from_file(path);
    if (!asset)
        return nullptr;

    double sample_rate = (double)asset->sample_instance.sample_rate;
    double max_time =
        min_time + samples_to_beat(asset->sample_instance.count, sample_rate, beat_duration);

    Clip* clip = track->add_audio_clip(path.filename().string(), min_time, max_time,
                                       {.asset = asset}, beat_duration);
    if (!clip) {
        asset->release();
        return nullptr;
    }

    return clip;
}

void Engine::process(AudioBuffer<float>& output_buffer, double sample_rate) {
    double buffer_duration = (double)output_buffer.n_samples / sample_rate;
    bool currently_playing = playing.load(std::memory_order_relaxed);

    if (currently_playing) {
        for (auto track : tracks) {
            track->event_queue.clear();
        }

        // Record a sequence of events from track clips.
        double inv_ppq = 1.0 / ppq;
        double current_beat_duration = 0.0;
        double old_playhead = playhead;
        do {
            double position = std::round(playhead * ppq) * inv_ppq;
            uint32_t buffer_offset =
                (uint32_t)((uint64_t)sample_position % output_buffer.n_samples);
            current_beat_duration = beat_duration.load(std::memory_order_relaxed);
            for (auto track : tracks) {
                track->process_event(buffer_offset, position, current_beat_duration, sample_rate);
            }
            playhead += inv_ppq;
            sample_position += beat_to_samples(inv_ppq, sample_rate, current_beat_duration);
        } while (playhead < old_playhead + (buffer_duration / current_beat_duration));
        playhead_ui.store(playhead, std::memory_order_release);
    }

    for (auto track : tracks) {
        track->process(output_buffer, sample_rate, currently_playing);
    }
}

Engine g_engine;

} // namespace wb