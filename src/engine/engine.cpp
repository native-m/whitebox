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
        playing = false;
    }

    std::optional<SampleAsset> Engine::get_or_load_sample_asset(const std::filesystem::path& path)
    {
        return sample_table.load_sample_from_file(path);
    }

    Track* Engine::add_track(TrackType type, const std::string& name)
    {
        std::scoped_lock lock(mtx_);
        Track* new_track = new Track(type, name);
        tracks.push_back(std::unique_ptr<Track>(new_track));
        return new_track;
    }

    Track* Engine::add_track_at(uint32_t index, TrackType type, const std::string& name)
    {
        std::scoped_lock lock(mtx_);
        Track* new_track = new Track(type, name);
        tracks.insert(tracks.begin() + index, std::unique_ptr<Track>(new_track));
        return new_track;
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
        for (auto& track : tracks)
            track->prepare_play(play_position);

        play_time = play_position;
        playing = true;
        playing.notify_all();
    }

    void Engine::stop()
    {
        playing = false;
        playing.notify_all();
        playhead_position_ = play_time;
    }

    void Engine::set_play_position(double new_position)
    {
        std::unique_lock lock(mtx_);

        for (auto& track : tracks)
            track->prepare_play(new_position);

        play_position = new_position;
        playhead_position_ = play_time;
    }

    double Engine::get_playhead_position() const
    {
        std::shared_lock lock(mtx_);
        return playhead_position_;
    }

    void Engine::prepare_audio(double sample_rate, uint32_t buffer_size)
    {

    }

    void Engine::process(AudioBuffer<float>& output_buffer, double sample_rate)
    {
        std::unique_lock lock(mtx_);
        double buffer_duration = (double)output_buffer.n_samples / sample_rate;

        // Prepare messages from track clips
        if (is_playing()) {
            uint32_t count = 0;
            const double tick_length = 1.0 / 96.0;
            double tmp_beat_duration = 0.0;

            // Clear before processing messages
            for (auto& track : tracks)
                track->message_queue.clear();

            for (;;) {
                tmp_beat_duration = beat_duration.load(std::memory_order_relaxed);
                double tick_duration = tmp_beat_duration / 96.0;
                for (auto& track : tracks)
                    track->process_message(play_time, tick_duration, sample_rate);
                play_time += tick_length;
                count++;

                if (play_time > playhead_position_ + (buffer_duration / tmp_beat_duration))
                    break;
            }

            playhead_position_ = playhead_position_ + (buffer_duration / tmp_beat_duration);
            Log::info("{}", count);
        }
    }
}