#include "asset.h"

#include <atomic>

#include "core/debug.h"
#include "extern/xxhash.h"

namespace wb {

AudioAsset::~AudioAsset() {
  delete waveform_visual;
}

void AudioAsset::release() {
  if (ref_count.load(std::memory_order_acquire) == 1 || ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    AssetManager::destroy_audio_asset(hash);
  }
}

//

void MidiAsset2::release() {
  if (ref_count == 0)
    return;
  if (ref_count-- == 1) {
    AssetManager::destroy_midi_asset(this);
  }
}

uint32_t MidiAsset2::find_first_note(double pos, uint32_t channel) {
  const MidiNoteBuffer& buffer = data.note_sequence;
  auto begin = buffer.begin();
  auto end = buffer.end();
  while (begin != end && pos >= begin->max_time) {
    begin++;
  }
  if (begin == end) {
    return (uint32_t)(-1);
  }
  return begin - buffer.begin();
}

//

std::unordered_map<uint64_t, AudioAsset> AssetManager::audio_assets;
Pool<MidiAsset2> AssetManager::midi_assets;
InplaceList<MidiAsset2> AssetManager::midi_asset_list;

AudioAsset* AssetManager::create_or_get_audio_asset(const std::string& asset_path) {
  if (asset_path.size() == 0)
    return {};

  uint64_t hash = XXH3_64bits(asset_path.data(), asset_path.size());
  auto item = audio_assets.find(hash);
  if (item != audio_assets.end()) {
    item->second.add_ref();
    return &item->second;
  }

  auto sample{ Sample::load_file(asset_path) };
  if (!sample)
    return {};

  auto sample_peaks{ WaveformVisual::create(&sample.value(), WaveformVisualQuality::High) };
  if (sample_peaks == nullptr)
    return {};

  auto asset = audio_assets.try_emplace(hash, hash, std::move(*sample), sample_peaks);
  return &asset.first->second;
}

MidiAsset2* AssetManager::create_midi_asset() {
  void* ptr = midi_assets.allocate();
  if (!ptr) {
    return nullptr;
  }
  MidiAsset2* asset = new (ptr) MidiAsset2();
  midi_asset_list.push_item(asset);
  return asset;
}

void AssetManager::destroy_audio_asset(uint64_t hash) {
  auto item = audio_assets.find(hash);
  if (item == audio_assets.end())
    return;
  audio_assets.erase(item);
}

void AssetManager::destroy_midi_asset(MidiAsset2* asset) {
  asset->remove_from_list();
  asset->~MidiAsset2();
  midi_assets.free(asset);
}

void AssetManager::shutdown() {
  for (auto& [hash, asset] : audio_assets)
    Log::debug("Sample asset leak: {}", asset.sample.path.string(), asset.ref_count.load(std::memory_order_relaxed));
  audio_assets.clear();

  while (auto asset = midi_asset_list.pop_next_item()) {
    auto midi_asset = static_cast<MidiAsset2*>(asset);
    Log::debug("Midi asset leak {:x}: {}", (uint64_t)midi_asset, midi_asset->ref_count);
    midi_asset->~MidiAsset2();
    midi_assets.free(midi_asset);
  }
}

}  // namespace wb