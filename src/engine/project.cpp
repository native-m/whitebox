#include "project.h"
#include "core/fs.h"
#include "engine/track.h"
#include "ui/timeline.h"
#include <algorithm>
#include <cstdio>

namespace wb {

static constexpr uint32_t project_header_version = 1;
static constexpr uint32_t project_info_version = 1;
static constexpr uint32_t project_sample_table_version = 1;
static constexpr uint32_t project_midi_table_version = 1;
static constexpr uint32_t project_track_version = 1;
static constexpr uint32_t project_clip_version = 1;

ProjectFileResult read_project_file(const std::filesystem::path& path, Engine& engine,
                                    SampleTable& sample_table, MidiTable& midi_table) {
    File file;
    uintmax_t size = std::filesystem::file_size(path);
    if (size < sizeof(ProjectHeader)) {
        return ProjectFileResult::ErrInvalidFormat;
    }
    if (!file.open(path, File::Read)) {
        return ProjectFileResult::ErrCannotAccessFile;
    }

    ProjectHeader header;
    if (file.read(&header, sizeof(ProjectHeader)) == 0) {
        return ProjectFileResult::ErrEndOfFile;
    }
    if (header.magic_numbers != 'RPBW') {
        return ProjectFileResult::ErrInvalidFormat;
    }
    if (header.version > project_header_version) {
        return ProjectFileResult::ErrIncompatibleVersion;
    }
    g_engine.set_bpm(header.initial_bpm);
    g_engine.set_playhead_position(header.playhead_pos);
    g_timeline.min_hscroll = header.timeline_view_min;
    g_timeline.max_hscroll = header.timeline_view_max;

    return ProjectFileResult::Ok;
}

ProjectFileResult write_midi_data(const MidiData& data) {
}

ProjectFileResult write_project_file(const std::filesystem::path& path, Engine& engine,
                                     SampleTable& sample_table, MidiTable& midi_table) {
    File file;
    if (!file.open(path, File::Write)) {
        return ProjectFileResult::ErrCannotAccessFile;
    }

    ProjectHeader header {
        .magic_numbers = 'RPBW',
        .version = project_header_version,
        .sample_count = (uint32_t)sample_table.samples.size(),
        .track_count = (uint32_t)engine.tracks.size(),
        .initial_bpm = engine.get_bpm(),
        .ppq = (uint32_t)engine.ppq,
        .playhead_pos = engine.playhead_pos(),
        .timeline_view_min = g_timeline.min_hscroll,
        .timeline_view_max = g_timeline.max_hscroll,
        .main_volume_db = 0.0f,
    };
    if (file.write(&header, sizeof(ProjectHeader)) == 0) {
        return ProjectFileResult::ErrCannotAccessFile;
    }

    ProjectInfo project_info {
        .version = project_info_version,
    };
    if (file.write_u32(project_info.version) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_buffer(project_info.author.data(), project_info.author.size()) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_buffer(project_info.title.data(), project_info.title.size()) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_buffer(project_info.description.data(), project_info.description.size()) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_buffer(project_info.genre.data(), project_info.genre.size()) < 4)
        return ProjectFileResult::ErrCannotAccessFile;

    // Sample table header
    if (file.write_u32('TSBW') < 4) // Magic number
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_u32(project_sample_table_version) < 4) // Version
        return ProjectFileResult::ErrCannotAccessFile;

    // Write sample paths
    uint32_t idx = 0;
    std::unordered_map<uint64_t, uint32_t> sample_index_map;
    for (auto& sample : sample_table.samples) {
        std::u8string path = sample.second.sample_instance.path.u8string();
        if (file.write_buffer(path.data(), path.size()) < 4)
            return ProjectFileResult::ErrCannotAccessFile;
        sample_index_map.emplace(sample.second.hash, idx);
        idx++;
    }
    idx = 0;

    // Midi table header
    if (file.write_u32('TMBW') < 4) // Magic number
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_u32(project_midi_table_version) < 4) // Version
        return ProjectFileResult::ErrCannotAccessFile;

    // Write midi assets
    std::unordered_map<MidiAsset*, uint32_t> midi_index_map;
    auto midi_asset_ptr = midi_table.allocated_assets.next_;
    while (auto asset = static_cast<MidiAsset*>(midi_asset_ptr)) {
        MidiData& data = asset->data;
        ProjectMidiAsset asset_header {
            .max_length = data.max_length,
            .channel_count = data.channel_count,
            .min_note = data.min_note,
            .max_note = data.max_note,
        };
        if (file.write(&asset_header, sizeof(ProjectMidiAsset)) < sizeof(ProjectMidiAsset))
            return ProjectFileResult::ErrCannotAccessFile;
        for (uint32_t i = 0; i < data.channel_count; i++) {
            // NOTE(native-m): Here we just dump midi note buffer to the file until the MidiNote
            // structure changed in the future.
            const MidiNoteBuffer& note_buffer = data.channels[i];
            uint32_t write_size = note_buffer.size() * sizeof(MidiNote);
            uint32_t total_size = sizeof(uint32_t) + write_size;
            if (file.write_buffer(note_buffer.data(), write_size) < total_size)
                return ProjectFileResult::ErrCannotAccessFile;
        }
        midi_index_map.emplace(asset, idx);
        idx++;
        midi_asset_ptr = asset->next_;
    }

    for (const auto track : engine.tracks) {
        ProjectTrack track_header {
            .magic_numbers = 'RTBW',
            .version = project_track_version,
            .flags =
                ProjectTrackFlags {
                    .shown = track->shown,
                    .has_name = track->name.size() != 0,
                    .mute = track->ui_parameter_state.mute,
                    .solo = track->ui_parameter_state.solo,
                },
            .color = track->color,
            .volume_db = track->ui_parameter_state.volume_db,
            .pan = track->ui_parameter_state.pan,
            .clip_count = (uint32_t)track->clips.size(),
        };
        if (file.write(&track_header, sizeof(ProjectTrack)) < sizeof(ProjectTrack))
            return ProjectFileResult::ErrCannotAccessFile;
        if (track_header.flags.has_name)
            if (file.write_buffer(track->name.data(), track->name.size()) < 4)
                return ProjectFileResult::ErrCannotAccessFile;

        for (const auto clip : track->clips) {
            ProjectClip clip_header {
                .magic_numbers = 'LCBW',
                .version = project_clip_version,
                .type = clip->type,
                .flags =
                    ProjectClipFlags {
                        .has_name = clip->name.size() != 0,
                        .active = clip->is_active(),
                    },
                .color = clip->color,
                .min_time = clip->min_time,
                .max_time = clip->max_time,
                .start_offset = clip->start_offset,
            };

            switch (clip->type) {
                case ClipType::Audio: {
                    uint64_t hash = clip->audio.asset->hash;
                    assert(sample_index_map.contains(hash) && "Sample asset index not found");
                    clip_header.audio = {
                        .fade_start = clip->audio.fade_start,
                        .fade_end = clip->audio.fade_end,
                        .asset_index = sample_index_map[hash],
                    };
                    break;
                }
                case ClipType::Midi: {
                    MidiAsset* asset = clip->midi.asset;
                    assert(midi_index_map.contains(asset) && "Midi asset index not found");
                    clip_header.midi.asset_index = midi_index_map[asset];
                    break;
                }
                default:
                    break;
            }

            if (file.write(&clip_header, sizeof(ProjectClip)) < sizeof(ProjectClip))
                return ProjectFileResult::ErrCannotAccessFile;
            if (clip_header.flags.has_name)
                if (file.write_buffer(clip->name.data(), clip->name.size()) < 4)
                    return ProjectFileResult::ErrCannotAccessFile;
        }
    }

    return ProjectFileResult::Ok;
}

} // namespace wb