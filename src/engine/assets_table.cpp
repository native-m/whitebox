#include "assets_table.h"
#include "core/algorithm.h"
#include "core/midi.h"
#include "gfx/renderer.h"

namespace wb {

void SampleAsset::release() {
    if (ref_count-- == 1)
        sample_table->destroy_sample(hash);
}

SampleAsset* SampleTable::load_from_file(const std::filesystem::path& path) {
    size_t hash = std::hash<std::filesystem::path> {}(path);

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
    samples.clear();
}

//

MidiAsset::MidiAsset(MidiTable* table) : midi_table(table) {
}

void MidiAsset::release() {
    if (ref_count-- == 1)
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

    /*uint32_t left = 0;
    uint32_t right = buffer.size();

    while (left < right) {
        uint32_t middle = (left + right) >> 1;
        bool comparison = buffer[middle].max_time < pos;
        uint32_t& side = (comparison ? left : right);
        middle += (uint32_t)comparison;
        side = middle;
    }

    return left;*/
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
    while (auto asset = allocated_assets.pop_tracked_resource()) {
        auto midi_asset = static_cast<MidiAsset*>(asset);
        midi_asset->~MidiAsset();
        midi_assets.free(midi_asset);
    }
}

SampleTable g_sample_table;
MidiTable g_midi_table;

} // namespace wb