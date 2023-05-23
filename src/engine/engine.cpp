#include "engine.h"
#include "engine.h"
#include "engine.h"
#include "../core/debug.h"
#include "../core/thread.h"
#include "../core/midi.h"
#include <functional>
#include <chrono>

using namespace std::chrono_literals;

namespace wb
{
    Engine::Engine()
    {
    }

    Engine::~Engine()
    {
        playing = true;
        stop_thread = true;
    }

    std::optional<SampleAsset> Engine::get_or_load_sample_asset(const std::filesystem::path& path)
    {
        return sample_table.load_sample_from_file(path);
    }

    void Engine::add_track(TrackType type, const std::string& name)
    {
        std::scoped_lock lock(mtx_);
        Track* new_track = new Track(type, name);
        tracks.push_back(std::unique_ptr<Track>(new_track));
    }

    void Engine::add_track_at(uint32_t index, TrackType type, const std::string& name)
    {
        std::scoped_lock lock(mtx_);
        Track* new_track = new Track(type, name);
        tracks.insert(tracks.begin() + index, std::unique_ptr<Track>(new_track));
    }

    AudioClip* Engine::add_audio_clip(Track* track, double min_time, double max_time)
    {
        WB_ASSERT(track);
        std::scoped_lock lock(mtx_);
        return track->add_audio_clip(min_time, max_time, nullptr);
    }

    void Engine::move_clip(Track* track, Clip* clip, double relative_pos)
    {
        WB_ASSERT(track && clip);
        std::scoped_lock lock(mtx_);
        track->move_clip(clip, relative_pos);
    }

    void Engine::resize_clip(Track* track, Clip* clip, double relative_pos, bool right_side)
    {
        WB_ASSERT(track && clip);
        std::scoped_lock lock(mtx_);
        track->resize_clip(clip, relative_pos, right_side);
    }

    void Engine::set_bpm(float bpm)
    {
        beat_duration.store(60.0 / (double)bpm, std::memory_order_release);
    }

    void Engine::play()
    {
        playing = true;
        playing.notify_all();
    }

    void Engine::stop()
    {
        playing = false;
        playing.notify_all();
        playback_position_.store(play_position, std::memory_order_release);
    }

    void Engine::seek_to(double new_position)
    {
        play_position = new_position;
        playback_position_.store(new_position, std::memory_order_release);
    }

    void Engine::process(AudioBuffer& output_buffer, double sample_rate)
    {
        if (is_playing()) {
            double buffer_duration = (double)output_buffer.n_samples / sample_rate;
            double last_playback_position = playback_position_.load(std::memory_order_relaxed);
            playback_position_.store(last_playback_position + (buffer_duration / beat_duration.load(std::memory_order_relaxed)));
        }
    }
}