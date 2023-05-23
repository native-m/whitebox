#pragma once

#include <thread>
#include <mutex>
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

        double play_position = 0;
        std::atomic<double> playback_position_;
        std::mutex mtx_;

        Engine();
        ~Engine();

        std::optional<SampleAsset> get_or_load_sample_asset(const std::filesystem::path& path);

        void add_track(TrackType type, const std::string& name);
        void add_track_at(uint32_t index, TrackType type, const std::string& name);
        AudioClip* add_audio_clip(Track* track, double min_time, double max_time);
        void move_clip(Track* track, Clip* clip, double relative_pos);
        void resize_clip(Track* track, Clip* clip, double relative_pos, bool right_side);

        void set_bpm(float tempo);
        void play();
        void stop();
        void seek_to(double new_position);

        inline bool is_playing() const { return playing.load(std::memory_order_relaxed); }
        inline double get_playback_position() const { return playback_position_.load(std::memory_order_relaxed); }

        // These functions should be called in audio thread
        void process(AudioBuffer& output_buffer, double sample_rate);
    };
}