#pragma once

#include <string>
#include <variant>
#include <optional>
#include <deque>
#include <imgui.h>
#include "clip.h"
#include "track_message.h"
#include "../core/memory.h"
#include "../core/audio_buffer.h"
#include "../core/local_queue.h"

namespace wb
{
    enum class TrackType
    {
        Audio,
        Midi
    };

    enum class TrackMessageType : uint8_t
    {
        AudioMessage,
        MIDIMessage
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
        // Clips stored as doubly-linked list
        ClipNode head_node;
        ClipNode tail_node;

        // Playback
        double playhead_position = -1.0;
        Clip* current_playing_clip = nullptr;
        Clip* first_played_clip = nullptr;
        Clip* last_played_clip = nullptr;
        LocalQueue<TrackMessage, 96> message_queue;

        Track(TrackType type, const std::string& name);
        ~Track();
        AudioClip* add_audio_clip(double min_time, double max_time, AudioClip* nearby_clip = nullptr);
        void move_clip(Clip* clip, double relative_pos);
        void resize_clip(Clip* clip, double relative_pos, bool right_side);
        Clip* find_clip(double time, Clip* nearby_clip = nullptr);
        Clip* seek_adjacent_clip(double time, Clip* hint = nullptr);
        Clip* seek_backward(double time, Clip* clip);
        Clip* seek_forward(double time, Clip* clip);

        void prepare_play(double position);
        void process_message(double current_position, double tick_duration, double sample_rate);

        void process(AudioBuffer<float>& output_buffer,
                     double sample_rate,
                     double tick_duration,
                     bool is_playing);

        void log_clip_ordering_();
    };
}