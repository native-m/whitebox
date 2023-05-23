#pragma once

#include "sample.h"
#include <unordered_map>
#include <optional>

namespace wb
{
    struct SampleTable;

    struct SampleRef
    {
        SampleTable* sample_table;
        size_t hash;
        uint32_t ref_count = 1;
        Sample sample_instance;

        SampleRef(SampleTable* sample_table, size_t hash, Sample&& sample);
        void add_ref() noexcept { ++ref_count; }
        void release();
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