#pragma once

#include <functional>

#include "audio_record.h"
#include "clip.h"
#include "clip_edit.h"
#include "core/audio_buffer.h"
#include "core/common.h"
#include "core/thread.h"
#include "core/timing.h"
#include "etypes.h"
#include "plughost/plugin_manager.h"

namespace wb {

struct Track;

struct ProjectInfo {
  std::string author;
  std::string title;
  std::string genre;
  std::string description;
};

struct Engine {
  using OnBpmChangeFn = std::function<void(double, double)>;

  uint32_t num_input_channels = 0;
  uint32_t num_output_channels = 0;
  uint32_t audio_buffer_size = 0;
  uint32_t audio_sample_rate = 0;
  double audio_buffer_duration_ms = 0;
  uint32_t audio_record_buffer_size = 64 * 1024;
  uint32_t audio_record_file_chunk_size = 8 * 1024;
  uint32_t audio_record_chunk_size = 256 * 1024;

  std::string project_filename = "untitled.wb";
  ProjectInfo project_info;
  std::vector<Track*> tracks;
  mutable Spinlock editor_lock;

  double ppq = 96.0;
  volatile double playhead{};
  double playhead_start{};
  double sample_position{};
  std::atomic<double> beat_duration;
  std::atomic<double> playhead_ui;
  std::atomic_bool playing;
  std::atomic_bool playhead_updated;
  std::atomic_bool has_deleted_clips;
  std::atomic_bool recording;
  std::vector<TrackInputGroup> track_input_groups;
  Vector<uint32_t> active_track_inputs;
  Vector<uint32_t> active_record_tracks;

  AudioBuffer<float> mixing_buffer;
  std::vector<OnBpmChangeFn> on_bpm_change_listener;

  AudioRecordQueue recorder_queue;
  Vector<Sample> recorded_samples;
  std::thread recorder_thread;

  PerformanceMeasurer perf_measurer;

  ~Engine();

  void set_bpm(double bpm);
  void set_playhead_position(double beat_position);
  void
  set_audio_channel_config(uint32_t input_channels, uint32_t output_channels, uint32_t buffer_size, uint32_t sample_rate);
  void clear_all();
  void play();
  void stop();
  void record();
  void stop_record();
  void arm_track_recording(uint32_t slot, bool armed);
  void set_track_input(uint32_t slot, TrackInputType mode, uint32_t index, bool armed);

  void edit_lock() {
    editor_lock.lock();
  }
  void edit_unlock() {
    editor_lock.unlock();
  }

  Track* add_track(const std::string& name);
  void delete_track(uint32_t slot);
  void delete_track(uint32_t first_slot, uint32_t count);
  void move_track(uint32_t from_slot, uint32_t to_slot);
  void solo_track(uint32_t slot);

  void preview_sample(const std::filesystem::path& path);

  TrackEditResult add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time);

  TrackEditResult add_audio_clip(
      Track* track,
      const std::string& name,
      double min_time,
      double max_time,
      double start_offset,
      const AudioClip& clip_info,
      bool active = true);

  TrackEditResult add_midi_clip(
      Track* track,
      const std::string& name,
      double min_time,
      double max_time,
      double start_offset,
      const MidiClip& clip_info,
      bool active = true);

  TrackEditResult emplace_clip(Track* track, const Clip& new_clip);
  TrackEditResult duplicate_clip(Track* track, Clip* clip_to_duplicate, double min_time, double max_time);
  TrackEditResult move_clip(Track* track, Clip* clip, double relative_pos);

  TrackEditResult resize_clip(
      Track* track,
      Clip* clip,
      double relative_pos,
      double min_resize_pos,
      double min_length,
      bool left_side,
      bool shift);

  TrackEditResult delete_clip(Track* track, Clip* clip);
  TrackEditResult add_to_cliplist(Track* track, Clip* clip);
  TrackEditResult delete_region(Track* track, double min, double max);
  std::optional<ClipQueryResult> query_clip_by_range(Track* track, double min, double max) const;

  TrackEditResult reserve_track_region(
      Track* track,
      uint32_t first_clip,
      uint32_t last_clip,
      double min,
      double max,
      bool dont_sort,
      Clip* ignore_clip);

  MultiEditResult create_midi_clips(
      const Vector<SelectedTrackRegion>& selected_track_regions,
      uint32_t first_track_idx,
      double min_pos,
      double max_pos);

  MultiEditResult move_or_duplicate_region(
      const Vector<SelectedTrackRegion>& selected_track_regions,
      uint32_t src_track_idx,
      int32_t dst_track_relative_idx,
      double min_pos,
      double max_pos,
      double relative_move_pos,
      bool duplicate);

  MultiEditResult resize_clips(
      const Vector<TrackClipResizeInfo>& clips,
      uint32_t first_track,
      double relative_pos,
      double resize_limit,
      double min_length,
      double min_resize_pos,
      bool left_side,
      bool shift);

  MultiEditResult shift_clips(
      const Vector<SelectedTrackRegion>& selected_track_regions,
      uint32_t first_track_idx,
      double relative_pos,
      double min_pos,
      double max_pos);

  MultiEditResult delete_region(
      const Vector<SelectedTrackRegion>& selected_track_regions,
      uint32_t first_track_idx,
      double min_pos,
      double max_pos,
      bool should_update_tracks = true);

  MidiEditResult add_note(
      uint32_t track_id,
      uint32_t clip_id,
      double min_time,
      double max_time,
      float velocity,
      int16_t note_key,
      uint16_t channel);

  MidiEditResult add_note(uint32_t track_id, uint32_t clip_id, uint32_t channel, const Vector<MidiNote>& midi_notes);

  MidiEditResult
  move_note(uint32_t track_id, uint32_t clip_id, uint32_t note_id, int32_t relative_key_pos, double relative_pos);

  MidiEditResult move_selected_note(uint32_t track_id, uint32_t clip_id, int32_t relative_key_pos, double relative_pos);

  MidiEditResult resize_note(uint32_t track_id, uint32_t clip_id, uint32_t note_id, double relative_pos, bool left_side);

  MidiEditResult resize_selected_note(uint32_t track_id, uint32_t clip_id, double relative_pos, bool left_side);

  std::optional<MidiEditResult>
  slice_note(uint32_t track_id, uint32_t clip_id, double slice_pos, float velocity, int16_t note_key, uint16_t channel);

  Vector<uint32_t> mute_selected_note(uint32_t track_id, uint32_t clip_id, bool should_mute);

  MidiEditResult delete_marked_notes(uint32_t track_id, uint32_t clip_id, bool selected);

  NoteSelectResult
  select_note(uint32_t track_id, uint32_t clip_id, double min_pos, double max_pos, int16_t min_key, int16_t max_key);

  NoteSelectResult select_or_deselect_notes(uint32_t track_id, uint32_t clip_id, bool should_select = true);

  void append_note_selection(uint32_t track_id, uint32_t clip_id, bool should_select, const Vector<uint32_t>& note_ids);

  void set_clip_gain(Track* track, uint32_t clip_id, float gain);

  PluginInterface* add_plugin_to_track(Track* track, PluginUID uid);
  void delete_plugin_from_track(Track* track);

  double get_song_length() const;

  void update_audio_visualization(float frame_rate);

  /*
      Process the whole thing.
      This runs on the audio thread.
  */
  void process(const AudioBuffer<float>& input_buffer, AudioBuffer<float>& output_buffer, double sample_rate);

  inline double playhead_pos() const {
    return playhead_ui.load(std::memory_order_relaxed);
  }

  inline double get_beat_duration() const {
    return beat_duration.load(std::memory_order_relaxed);
  }

  inline double get_bpm() const {
    return 60.0 / beat_duration.load(std::memory_order_relaxed);
  }

  inline bool is_playing() const {
    return playing.load(std::memory_order_relaxed);
  }

  inline bool is_recording() const {
    return recording.load(std::memory_order_relaxed);
  }

  template<typename Fn>
  void add_on_bpm_change_listener(Fn&& fn) {
    on_bpm_change_listener.push_back(fn);
  }

  Clip* get_midi_clip_(uint32_t track_id, uint32_t clip_id);

  void write_recorded_samples_(uint32_t num_samples);

  static void recorder_thread_runner_(Engine* engine);
};

extern Engine g_engine;

}  // namespace wb