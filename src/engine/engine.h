#pragma once

#include "../def.h"
#include "../stdpch.h"
#include "../core/audio_buffer.h"
#include "../core/thread.h"
#include "track.h"
#include "sample_table.h"

namespace wb
{
    enum class EngineCommandType
    {
        Play,
        Stop,
        Seek
    };

    struct EngineCommand
    {
        EngineCommandType type;
    };

    struct Engine
    {
        std::atomic<bool>                       playing;
        std::atomic<bool>                       stop_thread;
        std::atomic<double>                     beat_duration;
        SampleTable                             sample_table;
        std::vector<std::unique_ptr<Track>>     tracks;
        std::atomic_flag                        has_deleted_clips;

        uint64_t                                play_tick = 0;
        double                                  play_time = 0;
        double                                  play_position = 0;
        std::atomic<double>                     playhead_position_;
        ConcurrentRingBuffer<EngineCommand, 8>  command_queue_;
        mutable std::mutex                      player_mtx_;
        mutable Spinlock                        sync_;

        std::function<void()>                   on_bpm_change;

        Engine();
        ~Engine();

        std::optional<SampleAsset> get_or_load_sample_asset(const std::filesystem::path& path);

        Track* add_track(TrackType type, const std::string& name);
        Track* add_track_at(uint32_t index, TrackType type, const std::string& name);
        AudioClip* add_audio_clip(Track* track, double min_time, double max_time);
        void move_clip(Track* track, Clip* clip, double relative_pos);
        void resize_clip(Track* track, Clip* clip, double relative_pos, bool right_side);
        void delete_clip(Track* track, Clip* clip);

        void set_bpm(float tempo);
        void play();
        void stop();
        void set_play_position(double new_position);

        inline double get_playhead_position() const { return playhead_position_.load(std::memory_order_relaxed); }
        inline bool is_playing() const { return playing.load(std::memory_order_relaxed); }

        // These functions should be called in audio thread
        void prepare_audio(double sample_rate, uint32_t buffer_size);
        void process_command();
        void process(AudioBuffer<float>& output_buffer, double sample_rate);
    };
}