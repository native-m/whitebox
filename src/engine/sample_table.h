#pragma once

#include "core/common.h"
#include "sample.h"
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace wb {

using SampleHash = uint64_t;
struct SampleTable;

struct SampleAsset {
    SampleTable* sample_table;
    size_t hash;
    uint32_t ref_count = 1;
    Sample sample_instance;
    std::shared_ptr<SamplePeaks> peaks;

    inline void add_ref() noexcept { ++ref_count; }
    void release();
};

struct SampleTable {
    std::unordered_map<uint64_t, SampleAsset> samples;
    SampleAsset* load_sample_from_file(const std::filesystem::path& path);
    void destroy_sample(size_t hash);
};

extern SampleTable g_sample_table;
} // namespace wb