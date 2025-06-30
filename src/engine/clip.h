#pragma once

// #include <imgui.h>

#include <atomic>
#include <string>

#include "assets_table.h"
#include "core/color.h"
#include "core/common.h"

#define WB_INVALID_CLIP_ID (~0U)

namespace wb {

enum class ClipType {
  Unknown,
  Audio,
  Midi,
};

enum class ClipMode {
  OneShot,
  OneShotReverse,
  LoopStraight,
  LoopReverse,
  LoopBidirectional,
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
  double fade_start;
  double fade_end;
  double speed;
  float gain;
};

struct MidiClip {
  MidiAsset* asset;
  double length;
  int16_t transpose;
  int16_t rate;
  ClipMode mode;
};

struct Clip {
  uint32_t id{};

  // General clip information
  ClipType type{};
  std::string name{};
  Color color{};
  ClipHover hover_state{};
  std::atomic_bool active{ true };
  bool deleted{};
  volatile bool internal_state_changed{};

  // Time placement in beat units
  double min_time{};
  double max_time{};
  double start_offset{};  // MIDI: Beat unit, Audio: Sample unit

  union {
    AudioClip audio;
    MidiClip midi;
  };

  Clip() noexcept : id(WB_INVALID_CLIP_ID), audio() {
  }

  Clip(const std::string& name, const Color& color, double min_time, double max_time, double start_offset = 0.0) noexcept
      : id(WB_INVALID_CLIP_ID),
        type(ClipType::Unknown),
        name(name),
        color(color),
        min_time(min_time),
        max_time(max_time),
        start_offset(start_offset),
        audio() {
  }

  Clip(const Clip& clip) noexcept
      : id(clip.id),
        type(clip.type),
        name(clip.name),
        color(clip.color),
        active(clip.active.load(std::memory_order_relaxed)),
        min_time(clip.min_time),
        max_time(clip.max_time),
        start_offset(clip.start_offset) {
    switch (type) {
      case ClipType::Audio:
        audio = clip.audio;
        audio.asset->add_ref();
        break;
      case ClipType::Midi:
        midi = clip.midi;
        midi.asset->add_ref();
        break;
      default: break;
    }
  }

  Clip(Clip&& clip) noexcept
      : id(std::exchange(clip.id, {})),
        type(std::exchange(clip.type, {})),
        name(std::exchange(clip.name, {})),
        color(std::exchange(clip.color, {})),
        active(clip.active.exchange(false, std::memory_order_relaxed)),
        min_time(std::exchange(clip.min_time, {})),
        max_time(std::exchange(clip.max_time, {})),
        start_offset(std::exchange(clip.start_offset, {})) {
    switch (type) {
      case ClipType::Audio: audio = clip.audio; break;
      case ClipType::Midi: midi = clip.midi; break;
      default: break;
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
      default: break;
    }
  }

  Clip& operator=(const Clip& clip) noexcept {
    id = clip.id;
    type = clip.type;
    name = clip.name;
    color = clip.color;
    active = clip.active.load(std::memory_order_relaxed);
    min_time = clip.min_time;
    max_time = clip.max_time;
    start_offset = clip.start_offset;
    switch (type) {
      case ClipType::Audio: {
        SampleAsset* old_asset = audio.asset;
        audio = clip.audio;
        audio.asset->add_ref();
        if (old_asset)
          old_asset->release();
        break;
      }
      case ClipType::Midi: {
        MidiAsset* old_asset = midi.asset;
        midi = clip.midi;
        midi.asset->add_ref();
        if (old_asset)
          old_asset->release();
        break;
      }
      default: break;
    }
    return *this;
  }

  Clip& operator=(Clip&& clip) noexcept {
    id = clip.id;
    type = std::exchange(clip.type, {});
    name = std::exchange(clip.name, {});
    color = std::exchange(clip.color, {});
    active = clip.active.exchange(false, std::memory_order_relaxed);
    min_time = std::exchange(clip.min_time, {});
    max_time = std::exchange(clip.max_time, {});
    start_offset = std::exchange(clip.start_offset, {});
    switch (type) {
      case ClipType::Audio: audio = std::exchange(clip.audio, {}); break;
      case ClipType::Midi: midi = std::exchange(clip.midi, {}); break;
      default: break;
    }
    return *this;
  }

  inline void init_as_audio_clip(const AudioClip& clip_info) {
    type = ClipType::Audio;
    audio = clip_info;
  }

  inline void init_as_midi_clip(const MidiClip& clip_info) {
    type = ClipType::Midi;
    midi = clip_info;
  }

  inline void set_active(bool is_active) {
    active.store(is_active, std::memory_order_release);
  }

  inline void mark_deleted() {
    deleted = true;
  }

  inline MidiData* get_midi_data() {
    return (type == ClipType::Midi && midi.asset) ? &midi.asset->data : nullptr;
  }

  inline double get_asset_sample_rate() const {
    if (type == ClipType::Audio && audio.asset) {
      return audio.asset->sample_instance.sample_rate;
    }
    return 0.0;
  }

  inline double get_start_offset(double beat_duration) const {
    if (type == ClipType::Audio) {
      if (audio.asset == nullptr)
        return 0.0;
      return samples_to_beat(start_offset, (double)audio.asset->sample_instance.sample_rate, beat_duration);
    }
    return start_offset;
  }

  inline bool is_active() const {
    return active.load(std::memory_order_relaxed);
  }

  inline bool is_deleted() const {
    return deleted;
  }

  inline bool is_audio() const {
    return type == ClipType::Audio;
  }

  inline bool is_midi() const {
    return type == ClipType::Midi;
  }
};

}  // namespace wb