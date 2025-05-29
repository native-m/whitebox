#include "project.h"

#include <algorithm>

#include "core/byte_buffer.h"
#include "core/fs.h"
#include "core/serdes.h"
#include "core/stream.h"
#include "core/vector.h"
#include "engine/track.h"
#include "ui/browser.h"

#define WB_INVALID_ASSET_ID (~0U)

namespace wb {

static constexpr uint32_t project_header_version = 1;
static constexpr uint32_t project_info_version = 1;
static constexpr uint32_t project_sample_table_version = 1;
static constexpr uint32_t project_midi_table_version = 1;
static constexpr uint32_t project_track_version = 1;
static constexpr uint32_t project_clip_version = 2;

ProjectFileResult read_project_file(
    const std::filesystem::path& filepath,
    Engine& engine,
    SampleTable& sample_table,
    MidiTable& midi_table,
    TimelineWindow& timeline) {
  File file;
  if (!file.open(filepath, IOOpenMode::Read))
    return ProjectFileResult::ErrCannotAccessFile;

  MsgpackReader r(file);
  MsgpackView view = r.get_view();

  if (!view) {
    return ProjectFileResult::ErrInvalidFormat;
  }

  if (!view.is_map()) {
    return ProjectFileResult::ErrInvalidFormat;
  }

  if (auto project = view.map_find("wbpr")) {
    double initial_bpm = project.map_find("bpm").as_number(120.0);
    double playhead_pos = project.map_find("playhead_pos").as_number(0.0);

    engine.set_bpm(initial_bpm);
    engine.set_playhead_position(playhead_pos);
    timeline.min_hscroll = project.map_find("timeline_view_min").as_number(0.0);
    timeline.max_hscroll = project.map_find("timeline_view_max").as_number(1.0);

    if (auto p_info = project.map_find("project_info")) {
      engine.project_info.author = p_info.map_find("author").as_str();
      engine.project_info.title = p_info.map_find("title").as_str();
      engine.project_info.genre = p_info.map_find("genre").as_str();
      engine.project_info.description = p_info.map_find("desc").as_str();
    }

    Vector<SampleAsset*> sample_assets;
    if (auto samples = project.map_find("sample_table")) {
      uint32_t count = samples.array_size();

      for (uint32_t i = 0; i < count; i++) {
        std::string_view path_str = samples.array_get(i).as_str();
        if (path_str.empty()) {
          Log::error("Invalid path");
          return ProjectFileResult::ErrInvalidFormat;
        }

        std::filesystem::path sample_path(path_str);
        if (!std::filesystem::is_regular_file(sample_path)) {
          std::filesystem::path filename = sample_path.filename();
          bool found = false;
          Log::info("File not found: {}", filename.string());
          Log::info("Scanning {} in project relative path", filename.string());

          // Find sample file relative to project file or scan
          if (auto file = find_file_recursive(remove_filename_from_path(filepath), filename)) {
            sample_path = file.value();
            found = true;
          } else {
            Log::info("File {} not found in project relative path.", filename.string());
            for (const auto& directory : g_browser.directories) {
              Log::info("Scanning {} in user's directory: {}", filename.string(), directory.first->filename().string());
              if (auto file = find_file_recursive(*directory.first, filename)) {
                sample_path = file.value();
                found = true;
                break;
              }
            }
          }

          if (!found) {
            // TODO: Skip this sample if not found
            Log::error("Cannot find sample: {}", filename.string());
            sample_assets.push_back(nullptr);
            continue;
          }
        }

        Log::debug("({}) Loading sample: {}", i, sample_path.string());
        SampleAsset* asset = sample_table.load_from_file(sample_path);
        if (asset == nullptr)
          Log::error("Cannot open sample: {}", sample_path.filename().string());
        sample_assets.push_back(asset);
      }
    }

    Vector<MidiAsset*> midi_assets;
    if (auto midi_asset_array = project.map_find("midi_table")) {
      uint32_t count = midi_asset_array.array_size();
      for (uint32_t i = 0; i < count; i++) {
        if (auto midi = midi_asset_array.array_get(i)) {
          auto midi_notes = midi.map_find("notes");
          uint32_t note_count = midi_notes.array_size();
          MidiAsset* asset = midi_table.create_midi();
          MidiNoteBuffer& buffer = asset->data.note_sequence;
          uint32_t actual_count = 0;

          buffer.resize_fast(note_count);
          for (uint32_t j = 0; j < note_count; j++) {
            MidiNote& note = buffer[j];
            auto note_data = midi_notes.array_get(j);
            if (note_data.array_size() < 5) {
              Log::warn("Invalid note data, skipping");
              continue;
            }
            note.min_time = note_data.array_get(0).as_number(0.0);
            note.max_time = note_data.array_get(1).as_number(0.0);
            note.key = note_data.array_get(2).as_number<int16_t>();
            note.flags = note_data.array_get(3).as_number<uint16_t>();
            note.velocity = note_data.array_get(4).as_number(0.0f);
            actual_count++;
          }

          buffer.resize_fast(actual_count);
          asset->data.create_metadata(buffer.data(), buffer.size());
          asset->data.update_channel(0);
          midi_assets.push_back(asset);
        } else {
          midi_assets.push_back(nullptr);
        }
      }
    }

    if (auto tracks = project.map_find("tracks")) {
      uint32_t count = tracks.array_size();
      for (uint32_t i = 0; i < count; i++) {
        if (auto track_info = tracks.array_get(i)) {
          std::string name{ track_info.map_find("name").as_str() };
          ColorU32 col = track_info.map_find("col").as_number(0u);
          float height = track_info.map_find("height").as_number(0.0f);
          float vol = track_info.map_find("vol").as_number(0.0f);
          float pan = track_info.map_find("pan").as_number(0.0f);
          bool shown = track_info.map_find("shown").as_bool(true);
          bool solo = track_info.map_find("solo").as_bool(false);
          bool mute = track_info.map_find("mute").as_bool(false);

          Track* track = new (std::nothrow) Track("", col, height, shown, { .volume_db = vol, .pan = pan, .mute = mute });
          assert(track != nullptr);
          track->name = std::move(name);

          if (auto clips = track_info.map_find("clips")) {
            uint32_t clip_count = clips.array_size();
            track->clips.resize(clip_count);
            for (uint32_t j = 0; j < clip_count; j++) {
              if (auto clip_info = clips.array_get(j)) {
                ClipType type{ clip_info.map_find("type").as_number<uint8_t>() };
                std::string name{ clip_info.map_find("name").as_str() };
                Color color(clip_info.map_find("col").as_number(0u));
                bool active{ clip_info.map_find("active").as_bool(true) };
                double start{ clip_info.map_find("start").as_number(0.0) };
                double end{ clip_info.map_find("end").as_number(0.0) };
                double offset{ clip_info.map_find("ofs").as_number(0.0) };

                Clip* clip = track->allocate_clip();
                new (clip) Clip("", color, start, end, offset);
                clip->name = std::move(name);
                track->clips[j] = clip;

                if (auto data = clip_info.map_find("data")) {
                  uint32_t asset_id = data.map_find("asset_id").as_number(WB_INVALID_ASSET_ID);
                  switch (type) {
                    case ClipType::Audio:
                      if (asset_id != (uint32_t)-1) {
                        clip->init_as_audio_clip({
                          .asset = sample_assets[asset_id],
                          .fade_start = data.map_find("fstart").as_number(0.0),
                          .fade_end = data.map_find("fend").as_number(0.0),
                          .gain = data.map_find("gain").as_number(0.0f),
                        });
                      }
                      break;
                    case ClipType::Midi:
                      if (asset_id != WB_INVALID_ASSET_ID) {
                        clip->init_as_midi_clip({
                          .asset = midi_assets[asset_id],
                        });
                      }
                      break;
                    default: WB_UNREACHABLE();
                  }
                }
              }
            }
          }

          engine.tracks.push_back(track);
        }
      }
    }
  }

  return ProjectFileResult::Ok;
}

ProjectFileResult write_project_file(
    const std::filesystem::path& filepath,
    Engine& engine,
    SampleTable& sample_table,
    MidiTable& midi_table,
    TimelineWindow& timeline) {
  File file;
  if (!file.open(filepath, IOOpenMode::Write | IOOpenMode::Truncate))
    return ProjectFileResult::ErrCannotAccessFile;

  std::unordered_map<MidiAsset*, uint32_t> midi_index_map;
  std::unordered_map<SampleAsset*, uint32_t> sample_index_map;
  MsgpackWriter w(file);
  w.write_map(1);
  w.write_kv_map("wbpr", 10);
  w.write_kv_num("version", 1);
  w.write_kv_num("bpm", engine.get_bpm());
  w.write_kv_num("playhead_pos", engine.playhead_pos());
  w.write_kv_num("timeline_view_min", timeline.min_hscroll);
  w.write_kv_num("timeline_view_max", timeline.max_hscroll);
  w.write_kv_num("main_vol", 0.0f);

  w.write_kv_map("project_info", 4);
  {
    w.write_kv_str("author", engine.project_info.author);
    w.write_kv_str("title", engine.project_info.title);
    w.write_kv_str("genre", engine.project_info.genre);
    w.write_kv_str("desc", engine.project_info.description);
  }

  w.write_kv_array("sample_table", sample_table.samples.size());
  {
    uint32_t idx = 0;
    for (auto& sample : sample_table.samples) {
      std::string path = sample.second.sample_instance.path.string();
      w.write_str(path);
      sample_index_map.emplace(&sample.second, idx);
      idx++;
    }
  }

  w.write_kv_array("midi_table", midi_table.midi_assets.num_allocated);
  {
    uint32_t idx = 0;
    auto midi_asset_ptr = midi_table.allocated_assets.next_;
    while (auto asset = static_cast<MidiAsset*>(midi_asset_ptr)) {
      MidiData& data = asset->data;
      w.write_map(3);
      w.write_kv_num("min_note", data.min_note);
      w.write_kv_num("max_note", data.max_note);
      w.write_kv_array("notes", data.note_sequence.size());
      for (const MidiNote& note : data.note_sequence) {
        w.write_array(5);
        w.write_num(note.min_time);
        w.write_num(note.max_time);
        w.write_num(note.key);
        w.write_num(note.flags);
        w.write_num(note.velocity);
      }
      midi_index_map.emplace(asset, idx);
      midi_asset_ptr = asset->next_;
      idx++;
    }
  }

  w.write_kv_array("tracks", engine.tracks.size());
  {
    for (Track* track : engine.tracks) {
      w.write_map(9);
      w.write_kv_str("name", track->name);
      w.write_kv_num("col", track->color.to_uint32());
      w.write_kv_num("height", track->height);
      w.write_kv_num("vol", track->ui_parameter_state.volume_db);
      w.write_kv_num("pan", track->ui_parameter_state.pan);
      w.write_kv_bool("mute", track->ui_parameter_state.mute);
      w.write_kv_bool("solo", track->ui_parameter_state.solo);
      w.write_kv_bool("shown", track->shown);

      w.write_kv_array("clips", track->clips.size());
      for (Clip* clip : track->clips) {
        w.write_map(8);
        w.write_kv_num("type", (uint8_t)clip->type);
        w.write_kv_str("name", clip->name);
        w.write_kv_num("col", clip->color.to_uint32());
        w.write_kv_bool("active", clip->is_active());
        w.write_kv_num("start", clip->min_time);
        w.write_kv_num("end", clip->max_time);
        w.write_kv_num("ofs", clip->start_offset);

        switch (clip->type) {
          case ClipType::Audio:
            w.write_kv_map("data", 4);
            w.write_kv_num("asset_id", sample_index_map[clip->audio.asset]);
            w.write_kv_num("fstart", clip->audio.fade_start);
            w.write_kv_num("fend", clip->audio.fade_end);
            w.write_kv_num("gain", clip->audio.gain);
            break;
          case ClipType::Midi:
            w.write_kv_map("data", 1);
            w.write_kv_num("asset_id", midi_index_map[clip->midi.asset]);
            w.write_kv_num("trans", clip->midi.transpose);
            w.write_kv_num("rate", clip->midi.rate);
            break;
          default: WB_UNREACHABLE();
        }
      }
    }
  }

  return ProjectFileResult::Ok;
}

}  // namespace wb