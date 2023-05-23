#pragma once

#include <string>
#include <variant>
#include <optional>
#include <imgui.h>
#include "clip.h"
#include "../core/memory.h"

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
        // Clips stored as doubly-linked list
        ClipNode head_node;
        ClipNode tail_node;

        Track(TrackType type, const std::string& name);
        ~Track();
        AudioClip* add_audio_clip(double min_time, double max_time, AudioClip* nearby_clip = nullptr);
        void move_clip(Clip* clip, double relative_pos);
        void resize_clip(Clip* clip, double relative_pos, bool right_side);
        Clip* find_clip(double time, Clip* nearby_clip = nullptr);
        Clip* seek_adjacent_clip(double time, Clip* nearby_clip = nullptr);
        Clip* seek_backward(double time, Clip* clip);
        Clip* seek_forward(double time, Clip* clip);

        void log_clip_ordering_();
    };
}