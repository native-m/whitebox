#pragma once

#include "assets_table.h"
#include "core/common.h"
#include "engine.h"
#include "ui/timeline.h"
#include <filesystem>
#include <unordered_map>

namespace wb {

struct alignas(8) PFHeader {
    uint32_t magic_numbers;
    uint32_t version;
    uint32_t track_count;
    uint32_t sample_count;
    uint32_t midi_count;
    uint32_t ppq;
    double initial_bpm;
    double playhead_pos;
    double timeline_view_min;
    double timeline_view_max;
    float main_volume_db;
};

struct PFMidiAsset {
    double max_length;
    uint32_t channel_count;
    uint32_t min_note = 0;
    uint32_t max_note = 0;
};

union alignas(4) PFTrackFlags {
    struct {
        bool has_name : 1;
        bool shown : 1;
        bool mute : 1;
        bool solo : 1;
    };
    uint32_t u32;
};

struct alignas(8) PFTrackHeader {
    uint32_t magic_numbers;
    uint32_t version;
    PFTrackFlags flags;
    uint32_t color; // TODO: Use uint32_t color
    float view_height;
    float volume_db;
    float pan;
    uint32_t clip_count;
};

union alignas(4) PFClipFlags {
    struct {
        bool has_name : 1;
        bool active : 1;
        bool loop : 1;
    };
    uint32_t u32;
};

struct alignas(8) PFAudioClip {
    double fade_start;
    double fade_end;
    uint32_t asset_index;
};

struct alignas(8) PFMidiClip {
    uint32_t asset_index;
};

struct alignas(8) PFClipHeader {
    uint32_t magic_numbers;
    uint32_t version;
    ClipType type;
    PFClipFlags flags;
    uint32_t color;
    double min_time;
    double max_time;
    double start_offset;
    union {
        PFAudioClip audio;
        PFMidiClip midi;
    };
};

enum class ProjectFileResult {
    Ok,
    ErrCannotAccessFile,
    ErrCorruptedFile,
    ErrEndOfFile,
    ErrIncompatibleVersion,
    ErrInvalidFormat,
};

ProjectFileResult read_project_file(const std::filesystem::path& filepath, Engine& engine,
                                    SampleTable& sample_table, MidiTable& midi_table,
                                    GuiTimeline& timeline);
ProjectFileResult write_project_file(const std::filesystem::path& filepath, Engine& engine,
                                     SampleTable& sample_table, MidiTable& midi_table,
                                     GuiTimeline& timeline);

} // namespace wb