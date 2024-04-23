#include "sample_table.h"
#include "renderer.h"

namespace wb {

void SampleAsset::release() {
    if (ref_count-- == 1)
        sample_table->destroy_sample(hash);
}

SampleAsset* SampleTable::load_sample_from_file(const std::filesystem::path& path) {
    size_t hash = std::hash<std::filesystem::path> {}(path);

    // Try to find if it already exist or create new instance
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
        samples.try_emplace(hash, this, hash, 1, std::move(*new_sample), std::move(sample_peaks));

    return &asset.first->second;
}

void SampleTable::destroy_sample(size_t hash) {
    auto item = samples.find(hash);
    if (item == samples.end())
        return;
    samples.erase(item);
}

void SampleTable::shutdown() {
    samples.clear();
}

SampleTable g_sample_table;

} // namespace wb