#include "engine.h"
#include "engine.h"
#include "engine.h"
#include "../core/debug.h"
#include "../core/thread.h"
#include "../core/midi.h"
#include <functional>
#include <chrono>
#include <semaphore>

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
        std::scoped_lock lock(player_mtx_);
        Track* new_track = new Track(type, name);
        tracks.push_back(std::unique_ptr<Track>(new_track));
        return new_track;
    }

    Track* Engine::add_track_at(uint32_t index, TrackType type, const std::string& name)
    {
        std::scoped_lock lock(player_mtx_);
        Track* new_track = new Track(type, name);
        tracks.insert(tracks.begin() + index, std::unique_ptr<Track>(new_track));
        return new_track;
    }

    AudioClip* Engine::add_audio_clip(Track* track, double min_time, double max_time)
    {
        WB_ASSERT(track);
        std::scoped_lock lock(sync_);
        return track->add_audio_clip(min_time, max_time, nullptr);
    }

    void Engine::move_clip(Track* track, Clip* clip, double relative_pos)
    {
        WB_ASSERT(track && clip);
        std::scoped_lock lock(sync_);
        track->move_clip(clip, relative_pos);
    }

    void Engine::resize_clip(Track* track, Clip* clip, double relative_pos, bool right_side)
    {
        WB_ASSERT(track && clip);
        std::scoped_lock lock(sync_);
        track->resize_clip(clip, relative_pos, right_side);
    }

    void Engine::delete_clip(Track* track, Clip* clip)
    {
        WB_ASSERT(track && clip);
        std::scoped_lock lock(sync_);
        track->delete_clip(clip);
        has_deleted_clips.test_and_set(std::memory_order_acq_rel);
    }

    void Engine::set_bpm(float bpm)
    {
        beat_duration.store(60.0 / (double)bpm, std::memory_order_release);
        if (on_bpm_change)
            on_bpm_change();
    }

    void Engine::play()
    {
        command_queue_.push(EngineCommand{ .type = EngineCommandType::Play });
    }

    void Engine::stop()
    {
        command_queue_.push(EngineCommand{ .type = EngineCommandType::Stop });  
    }   

    void Engine::set_play_position(double new_position)
    {
        //for (auto& track : tracks)
        //    track->update_play_state(new_position);
        play_position = new_position;
        play_tick = 0;
        playhead_position_ = new_position;
    }
    
    void Engine::prepare_audio(double sample_rate, uint32_t buffer_size)
    {

    }

    void Engine::process_command()
    {
        EngineCommand* command = nullptr;
        while (command = command_queue_.pop()) {
            switch (command->type) {
                case EngineCommandType::Play:
                {
                    std::scoped_lock lock(sync_);
                    for (auto& track : tracks)
                        track->update_play_state(play_position);
                    play_time = play_position;
                    playing = true;
                    Log::info("----- Play -----");
                }
                break;
                case EngineCommandType::Stop:
                {
                    std::scoped_lock lock(sync_);
                    for (auto& track : tracks)
                        track->stop();
                    playhead_position_ = play_position;
                    playing = false;
                    Log::info("----- Stop -----");
                    break;
                }
            }
        }
    }

    void Engine::process(AudioBuffer<float>& output_buffer, double sample_rate)
    {
        process_command();

        std::scoped_lock player_lock(player_mtx_);
        bool playing = is_playing();
        double buffer_duration = (double)output_buffer.n_samples / sample_rate;

        output_buffer.clear();

        // Prepare messages from track clips
        if (playing) {
            static constexpr double tick_length = 1.0 / 96.0;
            double current_beat_duration = 0.0;

            // Clear before buffering new messages
            for (auto& track : tracks)
                track->message_queue.clear();
            
            for (;;) {
                std::scoped_lock lock(sync_);
                current_beat_duration = beat_duration.load(std::memory_order_relaxed);
                //double tick_duration = tmp_beat_duration / 96.0;
                for (auto& track : tracks)
                    track->process_message(play_position, std::round(play_time * 96.0) / 96.0, current_beat_duration, sample_rate);
                play_time += tick_length;
                if (play_time >= playhead_position_ + (buffer_duration / current_beat_duration))
                    break;
            }
            playhead_position_.store(playhead_position_ + (buffer_duration / current_beat_duration), std::memory_order_release);
        }

        for (auto& track : tracks)
            track->process(output_buffer, sample_rate, buffer_duration, playing);

        if (has_deleted_clips.test(std::memory_order_relaxed)) {
            for (auto& track : tracks)
                if (!track->deleted_clips.empty())
                    track->flush_deleted_clips();
            has_deleted_clips.clear(std::memory_order_release);
        }
    }
}