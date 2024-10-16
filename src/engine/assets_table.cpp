#include "assets_table.h"
#include "core/algorithm.h"
#include "core/midi.h"
#include "gfx/renderer.h"
#include "extern/xxhash.h"
#include "core/debug.h"

namespace wb {
static constexpr XXH64_hash_t sample_hash_seed = 69420;

void SampleAsset::release() {
    if (ref_count == 0)
        return;
    if (ref_count-- == 1 && !keep_alive)
        sample_table->destroy_sample(hash);
}

SampleAsset* SampleTable::load_from_file(const std::filesystem::path& path) {
    std::u8string str_path = path.u8string(); 
    uint64_t hash = XXH64(str_path.data(), str_path.size(), sample_hash_seed);
    
    auto item = samples.find(hash);
    if (item != samples.end()) {
        item->second.add_ref();
        return &item->second;
    }
    auto new_sample {Sample::load_file(path)};
    if (!new_sample)
        return {};

    auto sample_peaks {
        g_renderer->create_sample_peaks(new_sample.value(), SamplePeaksPrecision::High)};
    if (!sample_peaks)
        return {};

    auto asset =
        samples.try_emplace(hash, this, hash, 1u, std::move(*new_sample), std::move(sample_peaks));

    return &asset.first->second;
}

void SampleTable::destroy_sample(uint64_t hash) {
    auto item = samples.find(hash);
    if (item == samples.end())
        return;
    samples.erase(item);
}

void SampleTable::shutdown() {
    for (auto& [hash, sample] : samples) {
        Log::debug("Sample asset leak: {}", sample.ref_count);
    }
    samples.clear();
}

//

MidiAsset::MidiAsset(MidiTable* table) : midi_table(table) {
}

void MidiAsset::release() {
    if (ref_count == 0)
        return;
    if (ref_count-- == 1 && !keep_alive)
        midi_table->destroy(this);
}

uint32_t MidiAsset::find_first_note(double pos, uint32_t channel) {
    const MidiNoteBuffer& buffer = data.channels[channel];
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

MidiAsset* MidiTable::load_from_file(const std::filesystem::path& path) {
    MidiAsset* asset = create_midi();
    if (!asset) {
        return nullptr;
    }
    if (!load_notes_from_file(asset->data, path)) {
        destroy(asset);
        return nullptr;
    }
    return asset;
}

MidiAsset* MidiTable::create_midi() {
    void* ptr = midi_assets.allocate();
    if (!ptr) {
        return nullptr;
    }
    MidiAsset* asset = new (ptr) MidiAsset(this);
    allocated_assets.push_tracked_resource(asset);
    return asset;
}

void MidiTable::destroy(MidiAsset* asset) {
    assert(asset != nullptr);
    asset->remove_tracked_resource();
    asset->~MidiAsset();
    midi_assets.free(asset);
}

void MidiTable::shutdown() {
    uint32_t midi_id = 0;
    while (auto asset = allocated_assets.pop_tracked_resource()) {
        auto midi_asset = static_cast<MidiAsset*>(asset);
        Log::debug("Midi asset leak {:x}: {}", (uint64_t)midi_asset, midi_asset->ref_count);
        midi_asset->~MidiAsset();
        midi_assets.free(midi_asset);
    }
}

SampleTable g_sample_table;
MidiTable g_midi_table;

} // namespace wb