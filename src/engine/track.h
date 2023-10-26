#pragma once

#include "clip.h"
#include "track_message.h"
#include "../stdpch.h"
#include "../core/memory.h"
#include "../core/audio_buffer.h"
#include "../core/queue.h"
#include <imgui.h>

namespace wb
{
    enum class TrackType
    {
        Audio,
        Midi
    };

    struct Track
    {
        TrackType type;

        // UI Stuff
        std::string name;
        ImColor color;
        float height = 56.0f;
        bool shown = true;

        // Mixer
        bool active = true;
        float volume = 0.0f;
        float pan = 0.0f;

        // Clips
        std::optional<std::variant<Pool<AudioClip>, Pool<MIDIClip>>> clip_allocator;
        // Clips are stored as doubly-linked list
        Clip head_node;
        Clip tail_node;
        std::unordered_set<Clip*> deleted_clips;

        // Playback
        double last_position = -1.0;
        Clip* last_position_clip = nullptr;
        std::atomic<Clip*> next_clip = nullptr;
        std::atomic<Clip*> currently_playing_clip = nullptr;
        LocalQueue<TrackMessage, 96> message_queue;
        std::vector<TrackMessage> dbg_message;

        TrackMessage current_message{};
        TrackMessage last_message{};
        bool continue_message = false;
        bool has_current_message = false;
        bool has_succeeding_message = false;
        uint32_t samples_processed = 0;

        Track(TrackType type, const std::string& name);
        ~Track();
        AudioClip* add_audio_clip(double min_time, double max_time, AudioClip* nearby_clip = nullptr);
        void move_clip(Clip* clip, double relative_pos);
        void resize_clip(Clip* clip, double relative_pos, bool right_side);
        void delete_clip(Clip* clip);
        Clip* find_clip(double time, Clip* nearby_clip = nullptr);
        Clip* seek_adjacent_clip(double time, Clip* hint = nullptr);
        Clip* seek_backward(double time, Clip* clip);
        Clip* seek_forward(double time, Clip* clip);
        void flush_deleted_clips();

        void update_play_state(double position);
        void stop();
        void process_message(double offset, double current_position, double tick_duration, double sample_rate);

        void process(AudioBuffer<float>& output_buffer,
                     double sample_rate,
                     double buffer_duration,
                     bool is_playing);

        void play_sample(AudioBuffer<float>& output_buffer, TrackMessage& msg, uint32_t offset);
        void stop_sample(AudioBuffer<float>& output_buffer, TrackMessage& msg, TrackMessage& stop_msg, uint32_t offset);

        void log_clip_ordering_();
    };
}