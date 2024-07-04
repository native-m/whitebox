#pragma once

#include "core/common.h"
#include "core/vector.h"
#include "core/memory.h"
#include "core/midi.h"
#include "sample.h"
#include <filesystem>
#include <optional>
#include <array>
#include <unordered_map>

namespace wb {

using SampleHash = uint64_t;
struct SampleTable;
struct MidiTable;

struct SampleAsset {
    SampleTable* sample_table;
    uint64_t hash;
    uint32_t ref_count = 1;
    Sample sample_instance;
    std::shared_ptr<SamplePeaks> peaks;

    inline void add_ref() noexcept { ++ref_count; }
    void release();
};

struct MidiAsset {
    static constexpr uint32_t max_channels = 16;
    MidiTable* midi_table;
    std::array<MidiNoteBuffer, max_channels> channels;
    double max_length;
    uint32_t ref_count = 1;

    inline void add_ref() noexcept { ++ref_count; }
    void release();
};

struct SampleTable {
    std::unordered_map<uint64_t, SampleAsset> samples;
    SampleAsset* load_from_file(const std::filesystem::path& path);
    void destroy_sample(uint64_t hash);
    void shutdown();
};

struct MidiTable {
    Pool<MidiAsset> midi_assets;
    MidiAsset* load_from_file(const std::filesystem::path& path);
    MidiAsset* create_midi();
    void destroy(MidiAsset* asset);
    void shutdown();
};

extern SampleTable g_sample_table;
extern MidiTable g_midi_table;
} // namespace wb