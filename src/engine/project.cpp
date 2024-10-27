#include "project.h"
#include "core/fs.h"
#include "engine/track.h"
#include "ui/browser.h"
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

ProjectFileResult read_project_file(const std::filesystem::path& path, Engine& engine, SampleTable& sample_table,
                                    MidiTable& midi_table, GuiTimeline& timeline) {
    File file;
    uintmax_t size = std::filesystem::file_size(path);
    if (size < sizeof(PFHeader))
        return ProjectFileResult::ErrInvalidFormat;
    if (!file.open(path, File::Read))
        return ProjectFileResult::ErrCannotAccessFile;

    PFHeader header;
    if (file.read(&header, sizeof(PFHeader)) < sizeof(PFHeader))
        return ProjectFileResult::ErrCorruptedFile;
    if (header.magic_numbers != fourcc("WBPR"))
        return ProjectFileResult::ErrInvalidFormat;
    if (header.version > project_header_version)
        return ProjectFileResult::ErrIncompatibleVersion;
    engine.set_bpm(header.initial_bpm);
    engine.set_playhead_position(header.playhead_pos);
    g_timeline.min_hscroll = header.timeline_view_min;
    g_timeline.max_hscroll = header.timeline_view_max;

    ProjectInfo project_info {};
    uint32_t version;
    if (file.read_u32(&version) < 4)
        return ProjectFileResult::ErrCorruptedFile;
    if (version > project_info_version)
        return ProjectFileResult::ErrIncompatibleVersion;
    if (file.read_array(project_info.author) < 4)
        return ProjectFileResult::ErrCorruptedFile;
    if (file.read_array(project_info.title) < 4)
        return ProjectFileResult::ErrCorruptedFile;
    if (file.read_array(project_info.genre) < 4)
        return ProjectFileResult::ErrCorruptedFile;
    if (file.read_array(project_info.description) < 4)
        return ProjectFileResult::ErrCorruptedFile;

    // Read sample table header
    uint32_t sample_table_magic_number;
    uint32_t sample_table_version;
    if (file.read_u32(&sample_table_magic_number) < 4) // Magic number
        return ProjectFileResult::ErrCorruptedFile;
    if (sample_table_magic_number != fourcc("WBST"))
        return ProjectFileResult::ErrInvalidFormat;
    if (file.read_u32(&sample_table_version) < 4) // Version
        return ProjectFileResult::ErrCannotAccessFile;

    Log::info("Opening {} samples...", header.sample_count);
    std::u8string sample_path_str;
    Vector<SampleAsset*> sample_assets;
    sample_path_str.reserve(256);
    sample_assets.resize(header.sample_count);
    for (uint32_t i = 0; i < header.sample_count; i++) {
        if (file.read_array(sample_path_str) < 4)
            return ProjectFileResult::ErrCorruptedFile;
        std::filesystem::path sample_path(sample_path_str);
        // Check if this file exists. If not, do plan B or C.
        // Plan B: Scan the file in project relative path.
        // Plan C: Scan the file in user's directory path.
        if (!std::filesystem::is_regular_file(sample_path)) {
            std::filesystem::path filename = sample_path.filename();
            bool found = false;
            Log::info("File not found: {}", filename.string());
            Log::info("Scanning {} in project relative path", filename.string());
            // Find sample file relative to project file or scan
            if (auto file = find_file_recursive(remove_filename_from_path(path), filename)) {
                sample_path = file.value();
                found = true;
            } else {
                Log::info("File {} not found in project relative path.", filename.string());
                for (const auto& directory : g_browser.directories) {
                    Log::info("Scanning {} in user's directory: {}", filename.string(),
                              directory.first->filename().string());
                    if (auto file = find_file_recursive(*directory.first, filename)) {
                        sample_path = file.value();
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                Log::info("Cannot find sample: {}", filename.string());
            }
        }

        SampleAsset* asset = sample_table.load_from_file(sample_path);
        sample_assets[i] = asset;
        asset->ref_count = 0; // Must be zero for the first usage
    }

    // Read midi table header
    uint32_t midi_table_magic_number;
    uint32_t midi_table_version;
    if (file.read_u32(&midi_table_magic_number) < 4) // Magic number
        return ProjectFileResult::ErrCorruptedFile;
    if (midi_table_magic_number != fourcc("WBMT"))
        return ProjectFileResult::ErrInvalidFormat;
    if (file.read_u32(&midi_table_version) < 4) // Version
        return ProjectFileResult::ErrCorruptedFile;
    if (midi_table_version > project_midi_table_version)
        return ProjectFileResult::ErrIncompatibleVersion;

    Vector<MidiAsset*> midi_assets;
    midi_assets.resize(header.midi_count);
    for (uint32_t i = 0; i < header.midi_count; i++) {
        PFMidiAsset midi_asset;
        if (file.read(&midi_asset, sizeof(PFMidiAsset)) < sizeof(PFMidiAsset))
            return ProjectFileResult::ErrCorruptedFile;
        MidiAsset* asset = midi_table.create_midi();
        midi_asset.channel_count = math::min(midi_asset.channel_count, 16u);
        asset->data.max_length = midi_asset.max_length;
        asset->data.channel_count = midi_asset.channel_count;
        asset->data.min_note = midi_asset.min_note;
        asset->data.max_note = midi_asset.max_note;
        for (uint32_t j = 0; j < midi_asset.channel_count; j++) {
            auto& midi_note_buffer = asset->data.channels[j];
            if (file.read_array(midi_note_buffer) < 4)
                return ProjectFileResult::ErrCorruptedFile;
        }
        midi_assets[i] = asset;
    }

    engine.tracks.reserve(header.track_count);

    std::string tmp_str;
    for (uint32_t i = 0; i < header.track_count; i++) {
        PFTrackHeader track_header;
        if (file.read(&track_header, sizeof(PFTrackHeader)) < sizeof(PFTrackHeader))
            return ProjectFileResult::ErrCorruptedFile;
        if (track_header.magic_numbers != fourcc("WBTR"))
            return ProjectFileResult::ErrInvalidFormat;
        if (track_header.version > project_track_version)
            return ProjectFileResult::ErrIncompatibleVersion;

        Track* track = new (std::nothrow) Track();
        assert(track && "Cannot allocate track");

        if (track_header.flags.has_name)
            if (file.read_array(track->name) < 4)
                return ProjectFileResult::ErrCorruptedFile;

        track->color = ImColor(track_header.color);
        track->height = track_header.view_height;
        track->shown = track_header.flags.shown;
        track->ui_parameter_state.solo = track_header.flags.solo;
        track->set_volume(track_header.volume_db);
        track->set_pan(track_header.pan);
        track->set_mute(track_header.flags.mute);
        track->clips.resize(track_header.clip_count);

        for (uint32_t j = 0; j < track_header.clip_count; j++) {
            PFClipHeader clip_header;
            if (file.read(&clip_header, sizeof(PFClipHeader)) < sizeof(PFClipHeader))
                return ProjectFileResult::ErrCorruptedFile;
            if (clip_header.magic_numbers != fourcc("WBCL"))
                return ProjectFileResult::ErrInvalidFormat;
            if (clip_header.version > project_clip_version)
                return ProjectFileResult::ErrIncompatibleVersion;

            std::string name;
            if (clip_header.flags.has_name)
                if (file.read_array(name) < 4)
                    return ProjectFileResult::ErrCorruptedFile;

            Clip* clip = (Clip*)track->clip_allocator.allocate();
            assert(clip && "Cannot allocate clip");
            new (clip) Clip(std::move(name), ImColor(clip_header.color), clip_header.min_time, clip_header.max_time,
                            clip_header.start_offset);

            clip->id = j;
            switch (clip_header.type) {
                case ClipType::Audio: {
                    SampleAsset* asset = sample_assets[clip_header.audio.asset_index];
                    clip->init_as_audio_clip({
                        .asset = asset,
                        .fade_start = clip_header.audio.fade_start,
                        .fade_end = clip_header.audio.fade_end,
                    });
                    asset->add_ref();
                    break;
                }
                case ClipType::Midi:
                    clip->init_as_midi_clip({
                        .asset = midi_assets[clip_header.midi.asset_index],
                    });
                    break;
                default:
                    break;
            }

            track->clips[j] = clip;
        }

        engine.tracks.push_back(track);
    }

    return ProjectFileResult::Ok;
}

ProjectFileResult write_midi_data(const MidiData& data) {
    return ProjectFileResult::Ok;
}

ProjectFileResult write_project_file(const std::filesystem::path& path, Engine& engine, SampleTable& sample_table,
                                     MidiTable& midi_table, GuiTimeline& timeline) {
    File file;
    if (!file.open(path, File::Write | File::Truncate)) {
        return ProjectFileResult::ErrCannotAccessFile;
    }

    PFHeader header {
        .magic_numbers = fourcc("WBPR"),
        .version = project_header_version,
        .track_count = (uint32_t)engine.tracks.size(),
        .sample_count = (uint32_t)sample_table.samples.size(),
        .midi_count = (uint32_t)midi_table.midi_assets.num_allocated,
        .ppq = (uint32_t)engine.ppq,
        .initial_bpm = engine.get_bpm(),
        .playhead_pos = engine.playhead_pos(),
        .timeline_view_min = g_timeline.min_hscroll,
        .timeline_view_max = g_timeline.max_hscroll,
        .main_volume_db = 0.0f,
    };
    if (file.write(&header, sizeof(PFHeader)) == 0) {
        return ProjectFileResult::ErrCannotAccessFile;
    }

    ProjectInfo project_info {};
    if (file.write_u32(project_info_version) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_array(project_info.author) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_array(project_info.title) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_array(project_info.genre) < 4)
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_array(project_info.description) < 4)
        return ProjectFileResult::ErrCannotAccessFile;

    engine.project_info.author = std::move(project_info.author);
    engine.project_info.title = std::move(project_info.title);
    engine.project_info.description = std::move(project_info.description);
    engine.project_info.genre = std::move(project_info.genre);

    // Sample table header
    if (file.write_u32(fourcc("WBST")) < 4) // Magic number
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_u32(project_sample_table_version) < 4) // Version
        return ProjectFileResult::ErrCannotAccessFile;

    // Write sample paths
    uint32_t idx = 0;
    std::unordered_map<uint64_t, uint32_t> sample_index_map;
    for (auto& sample : sample_table.samples) {
        std::u8string path = sample.second.sample_instance.path.u8string();
        if (file.write_array(path) < 4)
            return ProjectFileResult::ErrCannotAccessFile;
        sample_index_map.emplace(sample.second.hash, idx);
        idx++;
    }
    idx = 0;

    // Midi table header
    if (file.write_u32(fourcc("WBMT")) < 4) // Magic number
        return ProjectFileResult::ErrCannotAccessFile;
    if (file.write_u32(project_midi_table_version) < 4) // Version
        return ProjectFileResult::ErrCannotAccessFile;

    // Write midi assets
    std::unordered_map<MidiAsset*, uint32_t> midi_index_map;
    auto midi_asset_ptr = midi_table.allocated_assets.next_;
    while (auto asset = static_cast<MidiAsset*>(midi_asset_ptr)) {
        MidiData& data = asset->data;
        PFMidiAsset asset_header {
            .max_length = data.max_length,
            .channel_count = data.channel_count,
            .min_note = data.min_note,
            .max_note = data.max_note,
        };
        if (file.write(&asset_header, sizeof(PFMidiAsset)) < sizeof(PFMidiAsset))
            return ProjectFileResult::ErrCannotAccessFile;
        for (uint32_t i = 0; i < data.channel_count; i++) {
            // NOTE(native-m): Here we just dump midi note buffer to the file until the MidiNote
            // structure changed in the future.
            const MidiNoteBuffer& note_buffer = data.channels[i];
            if (file.write_array(note_buffer) < 4)
                return ProjectFileResult::ErrCannotAccessFile;
        }
        midi_index_map.emplace(asset, idx);
        midi_asset_ptr = asset->next_;
        idx++;
    }

    for (const auto track : engine.tracks) {
        PFTrackHeader track_header {
            .magic_numbers = fourcc("WBTR"),
            .version = project_track_version,
            .flags =
                {
                    .has_name = track->name.size() != 0,
                    .shown = track->shown,
                    .mute = track->ui_parameter_state.mute,
                    .solo = track->ui_parameter_state.solo,
                },
            .color = (uint32_t)track->color,
            .view_height = track->height,
            .volume_db = track->ui_parameter_state.volume_db,
            .pan = track->ui_parameter_state.pan,
            .clip_count = (uint32_t)track->clips.size(),
        };
        if (file.write(&track_header, sizeof(PFTrackHeader)) < sizeof(PFTrackHeader))
            return ProjectFileResult::ErrEndOfFile;
        if (track_header.flags.has_name)
            if (file.write_array(track->name) < 4)
                return ProjectFileResult::ErrEndOfFile;

        for (const auto clip : track->clips) {
            PFClipHeader clip_header {
                .magic_numbers = fourcc("WBCL"),
                .version = project_clip_version,
                .type = clip->type,
                .flags =
                    {
                        .has_name = clip->name.size() != 0,
                        .active = clip->is_active(),
                    },
                .color = (uint32_t)clip->color,
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

            if (file.write(&clip_header, sizeof(PFClipHeader)) < sizeof(PFClipHeader))
                return ProjectFileResult::ErrEndOfFile;
            if (clip_header.flags.has_name)
                if (file.write_array(clip->name) < 4)
                    return ProjectFileResult::ErrCannotAccessFile;
        }
    }

    return ProjectFileResult::Ok;
}

} // namespace wb