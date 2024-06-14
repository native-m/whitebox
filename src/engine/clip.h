#pragma once

#include "core/common.h"
#include "sample_table.h"
#include <atomic>
#include <imgui.h>
#include <string>

#define WB_INVALID_CLIP_ID (~0U)

namespace wb {

enum class ClipType {
    Unknown,
    Audio,
    Midi,
};

struct AudioClip {
    SampleAsset* asset;
    double start_sample_pos;
};

struct MidiClip {
    uint32_t msg;
};

struct Clip {
    uint32_t id {};

    // General clip information
    ClipType type {};
    std::string name {};
    ImColor color {};
    mutable std::atomic_bool deleted {};

    // Time placement in beat units
    double min_time {};
    double max_time {};
    double relative_start_time = 0.0;

    union {
        AudioClip audio;
        MidiClip midi;
    };

    Clip() noexcept : id(WB_INVALID_CLIP_ID), audio() {}

    Clip(const std::string& name, const ImColor& color, double min_time, double max_time) noexcept :
        id(WB_INVALID_CLIP_ID),
        type(ClipType::Unknown),
        name(name),
        color(color),
        min_time(min_time),
        max_time(max_time),
        audio() {}

    Clip(const Clip& clip) noexcept :
        id(WB_INVALID_CLIP_ID),
        type(clip.type),
        name(clip.name),
        color(clip.color),
        min_time(clip.min_time),
        max_time(clip.max_time),
        relative_start_time(clip.relative_start_time) {
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

    inline void init_as_audio_clip(const AudioClip& clip_info) {
        type = ClipType::Audio;
        audio = clip_info;
    }

    inline void as_midi_clip() {
        type = ClipType::Midi;
    }

    inline void mark_deleted() { deleted.store(true, std::memory_order_release); }

    inline bool is_deleted() const { return deleted.load(std::memory_order_relaxed); }
};

} // namespace wb