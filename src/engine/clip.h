#pragma once

#include "core/common.h"
#include "sample_table.h"
#include <imgui.h>
#include <string>

#define WB_INVALID_TRACK_ID (~0UL)

namespace wb {

enum class ClipType {
    Unknown,
    Audio,
    Midi,
};

struct AudioClip {
    SampleAsset* asset;
    double min_sample_pos;
    double start_offset;
};

struct MidiClip {};

struct Clip {
    uint32_t id;

    // General clip information
    ClipType type;
    std::string name;
    ImColor color;

    // Time placement in beat units
    double min_time;
    double max_time;
    double relative_start_time = 0.0;

    union {
        AudioClip audio;
        MidiClip midi;
    };

    Clip(const std::string& name, const ImColor& color, double min_time, double max_time) noexcept :
        id(WB_INVALID_TRACK_ID),
        type(ClipType::Unknown),
        name(name),
        color(color),
        min_time(min_time),
        max_time(max_time),
        audio() {}

    Clip(const Clip& clip) noexcept :
        id(WB_INVALID_TRACK_ID),
        type(clip.type),
        name(clip.name),
        color(clip.color),
        min_time(clip.min_time),
        max_time(clip.max_time) {
        switch (type) {
            case ClipType::Audio:
                audio = clip.audio;
                audio.asset->add_ref();
                break;
            case ClipType::Midi:
                break;
            default:
                WB_UNREACHABLE();
        }
    }

    ~Clip() {
        switch (type) {
            case ClipType::Audio:
                if (audio.asset)
                    audio.asset->release();
                break;
            case ClipType::Midi:
                break;
            default:
                WB_UNREACHABLE();
        }
    }

    void as_audio_clip(const AudioClip& clip_info) {
        type = ClipType::Audio;
        audio = clip_info;
    }

    void as_midi_clip() {}
};

} // namespace wb