#pragma once

#include <atomic>
#include <unordered_map>
#include <string>

#include "core/common.h"
#include "core/memory.h"
#include "core/list.h"
#include "dsp/sample.h"
#include "gfx/waveform_visual.h"
#include "midi_data.h"

namespace wb {

struct Clip;

struct AudioAsset {
  uint64_t hash;
  Sample sample;
  WaveformVisual* waveform_visual;
  InplaceList<Clip> clip_refs;
  std::atomic_uint32_t ref_count;

  AudioAsset(uint64_t hash, Sample&& sample, WaveformVisual* waveform_visual)
      : hash(hash),
        sample(std::forward<Sample>(sample)),
        waveform_visual(waveform_visual),
        ref_count(1u) {
  }

  ~AudioAsset();

  void add_ref() {
    ref_count.fetch_add(1, std::memory_order_relaxed);
  }

  void release();
};

struct MidiAsset2 : public InplaceList<MidiAsset2> {
  MidiData data{};
  InplaceList<Clip> clip_refs;
  uint32_t ref_count;

  void add_ref() {
    ++ref_count;
  }

  void release();

  uint32_t find_first_note(double pos, uint32_t channel);
};

struct AssetManager {
  static std::unordered_map<uint64_t, AudioAsset> audio_assets;
  static Pool<MidiAsset2> midi_assets;
  static InplaceList<MidiAsset2> midi_asset_list;

  static AudioAsset* create_or_get_audio_asset(const std::string& asset_path);
  static MidiAsset2* create_midi_asset_from_file(const std::string& asset_path);
  static MidiAsset2* create_midi_asset();
  static void destroy_audio_asset(uint64_t hash);
  static void destroy_midi_asset(MidiAsset2* asset);
  static void shutdown();
};

}  // namespace wb