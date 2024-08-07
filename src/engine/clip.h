#pragma once

#include "core/common.h"
#include "assets_table.h"
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

enum class ClipHover {
    None,
    All,
    LeftHandle,
    RightHandle,
    FadeStartHandle,
    FadeEndHandle,
};

struct AudioClip {
    SampleAsset* asset;
    double sample_offset;
    double fade_start;
    double fade_end;
};

struct MidiClip {
    MidiAsset* asset;
    uint32_t msg;
};

struct Clip {
    uint32_t id {};

    // General clip information
    ClipType type {};
    std::string name {};
    ImColor color {};
    ClipHover hover_state {};
    std::atomic_bool active {true};
    mutable std::atomic_bool deleted {};

    // Time placement in beat units
    double min_time {};
    double max_time {};
    double relative_start_time {};

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
                midi = clip.midi;
                midi.asset->add_ref();
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
                if (midi.asset)
                    midi.asset->release();
                break;
            default:
                WB_UNREACHABLE();
        }
    }

    inline void init_as_audio_clip(const AudioClip& clip_info) {
        type = ClipType::Audio;
        audio = clip_info;
    }

    inline void init_as_midi_clip(const MidiClip& clip_info) {
        type = ClipType::Midi;
        midi = clip_info;
    }

    inline void set_active(bool is_active) { active.store(is_active, std::memory_order_release); }

    inline void mark_deleted() { deleted.store(true, std::memory_order_release); }

    inline bool is_active() const { return active.load(std::memory_order_relaxed); }

    inline bool is_deleted() const { return deleted.load(std::memory_order_relaxed); }

    inline bool is_audio() const { return type == ClipType::Audio; }

    inline bool is_midi() const { return type == ClipType::Midi; }
};

} // namespace wb