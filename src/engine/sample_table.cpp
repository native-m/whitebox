#include "sample_table.h"
#include "../renderer.h"

namespace wb
{
    SampleRef::SampleRef(SampleTable* sample_table, size_t hash, Sample&& sample, std::shared_ptr<SamplePeaks>& peaks) :
        sample_table(sample_table),
        hash(hash),
        sample_instance(std::move(sample)),
        peaks(peaks)
    {
    }

    void SampleRef::release()
    {
        if (ref_count-- == 1)
            sample_table->destroy_sample(hash);
    }

    //

    std::optional<SampleAsset> SampleTable::load_sample_from_file(const std::filesystem::path& path)
    {
        size_t hash = std::hash<std::filesystem::path>{}(path);
        auto item = samples.find(hash);
        if (item != samples.end()) {
            item->second.add_ref(); // increment reference counter
            return { SampleAsset(&item->second) };
        }

        auto new_sample(Sample::load_file(path));
        if (!new_sample)
            return {};

        auto sample_peaks{ Renderer::instance->create_sample_peaks(new_sample.value(), PixelFormat::R_8_SNORM, GPUMemoryType::Device) };
        if (!sample_peaks)
            return {};

        auto result = samples.try_emplace(hash, this, hash, std::move(*new_sample), sample_peaks);
        return SampleAsset(&result.first->second);
    }

    void SampleTable::destroy_sample(size_t hash)
    {
        auto item = samples.find(hash);
        if (item == samples.end())
            return;
        samples.erase(item);
    }
}