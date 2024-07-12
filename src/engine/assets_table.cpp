#include "assets_table.h"
#include "renderer.h"
#include "core/midi.h"

namespace wb {

void SampleAsset::release() {
    if (ref_count-- == 1)
        sample_table->destroy_sample(hash);
}

SampleAsset* SampleTable::load_from_file(const std::filesystem::path& path) {
    size_t hash = std::hash<std::filesystem::path> {}(path);

    auto new_sample {Sample::load_file(path)};
    if (!new_sample)
        return {};

    auto item = samples.find(hash);
    if (item != samples.end()) {
        item->second.add_ref();
        return &item->second;
    }

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
    samples.clear();
}

//

void MidiAsset::release() {
    if (ref_count-- == 1)
        midi_table->destroy(this);
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
    return new (ptr) MidiAsset();
}

void MidiTable::destroy(MidiAsset* asset) {
    assert(asset != nullptr);
    asset->~MidiAsset();
    midi_assets.free(asset);
}

void MidiTable::shutdown() {
}

SampleTable g_sample_table;
MidiTable g_midi_table;

} // namespace wb