#pragma once

#include "sample.h"
#include "../waveform_view_buffer.h"
#include "../stdpch.h"

namespace wb
{
    struct SampleTable;

    struct SampleRef
    {
        SampleTable* sample_table;
        size_t hash;
        uint32_t ref_count = 1;
        Sample sample_instance;
        std::shared_ptr<SamplePeaks> peaks;

        SampleRef(SampleTable* sample_table, size_t hash, Sample&& sample, std::shared_ptr<SamplePeaks>& peaks);
        inline void add_ref() noexcept { ++ref_count; }
        inline void release();
    };

    struct SampleAsset
    {
        SampleRef* ref;

        SampleAsset(SampleRef* ref = nullptr) : ref(ref) {};
        
        SampleAsset(const SampleAsset& other) noexcept :
            ref(other.ref)
        {
            ref->add_ref();
        }

        SampleAsset(SampleAsset&& other) noexcept :
            ref(other.ref)
        {
            other.ref = nullptr;
        }

        ~SampleAsset()
        {
            if (ref) ref->release();
        }

        SampleAsset& operator=(const SampleAsset& other)
        {
            ref = other.ref;
            ref->add_ref();
            return *this;
        }

        SampleAsset& operator=(SampleAsset&& other) noexcept
        {
            ref = other.ref;
            other.ref = nullptr;
            return *this;
        }

        inline Sample* operator->() noexcept { return &ref->sample_instance; }
        inline const Sample* operator->() const noexcept { return &ref->sample_instance; }
        inline Sample& operator*() noexcept { return ref->sample_instance; }
        inline const Sample& operator*() const noexcept { return ref->sample_instance; }
    };

    struct SampleTable
    {
        std::unordered_map<uint64_t, SampleRef> samples;

        std::optional<SampleAsset> load_sample_from_file(const std::filesystem::path& path);
        void destroy_sample(size_t hash);

        //void get_sample_by_file_path(std::file)
    };
}