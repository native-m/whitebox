#pragma once

#include "core/common.h"
#include "core/list.h"
#include "core/memory.h"
#include "core/midi.h"
#include "core/vector.h"
#include "sample.h"
#include "gfx/waveform_visual.h"
#include <array>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace wb {

using SampleHash = uint64_t;
struct SampleTable;
struct MidiTable;

struct SampleAsset {
    SampleTable* sample_table;
    uint64_t hash;
    uint32_t ref_count = 1u;
    Sample sample_instance;
    WaveformVisual* peaks {};
    bool keep_alive = false;

    inline void add_ref() noexcept { ++ref_count; }
    void release();
};

struct MidiAsset : public InplaceList<MidiAsset> {
    MidiTable* midi_table;
    MidiData data {};
    uint32_t ref_count = 1;
    bool keep_alive = false;

    MidiAsset(MidiTable* table);
    inline void add_ref() noexcept { ++ref_count; }
    void release();
    uint32_t find_first_note(double pos, uint32_t channel);
};

struct SampleTable {
    std::unordered_map<uint64_t, SampleAsset> samples;
    SampleAsset* create_from_existing_sample(Sample&& sample);
    SampleAsset* load_from_file(const std::filesystem::path& path);
    void destroy_sample(uint64_t hash);
    void destroy_unused();
    void shutdown();
};

struct MidiTable {
    Pool<MidiAsset> midi_assets;
    InplaceList<MidiAsset> allocated_assets;
    MidiAsset* load_from_file(const std::filesystem::path& path);
    MidiAsset* create_midi();
    void destroy(MidiAsset* asset);
    void shutdown();
};

extern SampleTable g_sample_table;
extern MidiTable g_midi_table;
} // namespace wb