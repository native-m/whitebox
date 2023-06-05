#pragma once

#include <thread>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <memory>
#include "../def.h"
#include "../core/audio_buffer.h"
#include "track.h"
#include "sample_table.h"

namespace wb
{
    struct Engine
    {
        std::atomic<bool> playing;
        std::atomic<bool> stop_thread;
        std::atomic<double> beat_duration;
        SampleTable sample_table;
        std::vector<std::unique_ptr<Track>> tracks;

        double play_time = 0;
        double play_position = 0;
        double playhead_position_;
        mutable std::shared_mutex mtx_;

        Engine();
        ~Engine();

        std::optional<SampleAsset> get_or_load_sample_asset(const std::filesystem::path& path);

        Track* add_track(TrackType type, const std::string& name);
        Track* add_track_at(uint32_t index, TrackType type, const std::string& name);
        AudioClip* add_audio_clip(Track* track, double min_time, double max_time);
        void move_clip(Track* track, Clip* clip, double relative_pos);
        void resize_clip(Track* track, Clip* clip, double relative_pos, bool right_side);

        void set_bpm(float tempo);
        void play();
        void stop();
        void set_play_position(double new_position);

        double get_playhead_position() const;
        inline bool is_playing() const { return playing.load(std::memory_order_relaxed); }

        // These functions should be called in audio thread
        void prepare_audio(double sample_rate, uint32_t buffer_size);
        void process(AudioBuffer<float>& output_buffer, double sample_rate);
    };
}