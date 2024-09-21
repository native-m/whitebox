#pragma once

#include "assets_table.h"
#include "core/common.h"
#include "core/fs.h"
#include "core/vector.h"
#include "engine.h"
#include <fstream>
#include <unordered_map>

namespace wb {

struct alignas(8) ProjectHeader {
    uint32_t magic_numbers;
    uint32_t version;
    uint32_t track_count;
    uint32_t midi_count;
    uint32_t sample_count;
    double initial_bpm;
    double ppq;
    double playhead_pos;
    double timeline_view_min;
    double timeline_view_max;
    float main_volume_db;
};

struct ProjectInfo {
    static constexpr uint32_t project_info_version = 1;
    uint32_t version;
    std::string author;
    std::string title;
    std::string description;
    std::string genre;
};

struct ProjectSampleTable {
    std::unordered_map<uint64_t, uint32_t> sample_index_map;
    std::vector<SampleAsset*> sample_asset;
};

struct ProjectMidiAsset {
    double max_length;
    uint32_t channel_count;
    uint32_t min_note = 0;
    uint32_t max_note = 0;
};

union alignas(4) ProjectTrackFlags {
    struct {
        bool has_name : 1;
        bool shown : 1;
        bool mute : 1;
        bool solo : 1;
    };
    uint32_t flags;
};

struct alignas(8) ProjectTrack {
    uint32_t magic_numbers;
    uint32_t version;
    ProjectTrackFlags flags;
    ImVec4 color;
    float volume_db;
    float pan;
    uint32_t clip_count;
};

union alignas(4) ProjectClipFlags {
    struct {
        bool has_name : 1;
        bool active : 1;
        bool loop : 1;
    };
    uint32_t flags;
};

struct alignas(8) ProjectAudioClip {
    double fade_start;
    double fade_end;
    uint32_t asset_index;
};

struct alignas(8) ProjectMidiClip {
    uint32_t asset_index;
};

struct alignas(8) ProjectClip {
    uint32_t magic_numbers;
    uint32_t version;
    ClipType type;
    ProjectClipFlags flags;
    ImVec4 color;
    double min_time;
    double max_time;
    double start_offset;
    union {
        ProjectAudioClip audio;
        ProjectMidiClip midi;
    };
};

enum class ProjectFileResult {
    Ok,
    ErrCannotAccessFile,
    ErrEndOfFile,
    ErrIncompatibleVersion,
    ErrInvalidFormat,
};

ProjectFileResult read_project_file(const std::filesystem::path& filepath, Engine& engine,
                                    SampleTable& sample_table, MidiTable& midi_table);
ProjectFileResult write_project_file(const std::filesystem::path& filepath, Engine& engine,
                                     SampleTable& sample_table, MidiTable& midi_table);

} // namespace wb