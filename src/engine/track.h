#pragma once

#include <array>
#include <numbers>
#include <optional>
#include <random>

#include "audio_param.h"
#include "clip.h"
#include "core/audio_buffer.h"
#include "core/bit_manipulation.h"
#include "core/memory.h"
#include "core/vector.h"
#include "dsp/param_queue.h"
#include "etypes.h"
#include "event.h"
#include "event_list.h"
#include "midi_voice.h"
#include "plughost/plugin_interface.h"
#include "test_synth.h"
#include "track_input.h"
#include "vu_meter.h"

namespace wb {

struct Track;

enum TrackParameter {
  TrackParameter_Volume,
  TrackParameter_Pan,
  TrackParameter_Mute,
  TrackParameter_Max,
};

struct TrackEventState {
  std::optional<uint32_t> current_clip_idx;
  std::optional<uint32_t> clip_idx;
  Clip* current_clip;
  double last_start_clip_position;
  uint32_t midi_note_idx;
  bool refresh_voice;
  bool partially_ended;
};

struct TrackParameterState {
  float volume_db;  // UI only
  float volume;
  float pan;
  float pan_coeffs[2];  // Audio only
  bool mute;
  bool solo;  // UI only
};

struct TrackAutomation {
  int32_t plugin_id;
  uint32_t id;
  double value;
};

struct TrackParamChange {
  uint32_t id;
  double value;
};

struct TrackPluginParamChange {
  uint32_t id;
  double value;
  PluginInterface* plugin;
};

struct TrackMessage {
  enum {
    ParamChange,
    PluginParamChange,
    MidiNoteOn,
    MidiNoteOff,
  };

  uint32_t type;

  union {
    TrackPluginParamChange plugin_param_change;
    TrackParamChange param_change;
    MidiNoteOnEvent midi_note_on;
    MidiNoteOffEvent midi_note_off;
  };
};

struct Track {
  std::string name;
  Color color{ 0.3f, 0.3f, 0.3f, 1.0f };
  float height = 60.0f;
  bool shown = true;
  TrackInput input{};
  TrackInputAttr input_attr{ this };

  uint32_t recording_buffer_id = 0;
  uint32_t recording_session_id = 0;
  double record_min_time = 0.0;
  double record_max_time = 0.0;
  size_t num_samples_written = 0;
  std::optional<Sample> recorded_samples;

  Pool<Clip> clip_allocator;
  Vector<Clip*> clips;
  Vector<Clip*> deleted_clips;
  bool has_deleted_clips = false;

  TrackEventState event_state{};
  Vector<AudioEvent> audio_event_buffer;
  AudioEvent current_audio_event{};
  size_t samples_processed{};
  AudioBuffer<float> effect_buffer{};

  MidiVoiceState midi_voice_state{};
  MidiEventList midi_event_list;
  TestSynth test_synth{};

  LevelMeterColorMode level_meter_color{};
  VUMeter level_meter[2]{};

  PluginHandler plugin_handler{ plugin_begin_edit, plugin_perform_edit, plugin_end_edit };
  PluginInterface* plugin_instance = nullptr;
  uint32_t default_input_bus = 0;
  uint32_t default_output_bus = 0;

  TrackParameterState ui_parameter_state{};  // UI-side state
  TrackParameterState parameter_state{};     // Audio-side state
  dsp::ParamQueue param_queue;
  ConcurrentRingBuffer<TrackMessage> track_msg_queue;

  Track();
  Track(const std::string& name, const Color& color, float height, bool shown, const TrackParameterState& track_param);
  ~Track();

  void set_volume(float db);
  void set_pan(float pan);
  void set_mute(bool mute);
  void send_note_message(bool on_off, int16_t key, float velocity);
  void send_message(const TrackMessage& msg);

  inline float get_height() const {
    return shown ? height : 20.0f;
  }

  inline bool has_clips() const {
    return !clips.empty();
  }

  /**
   * @brief Allocate clip. The callee must construct Clip object itself.
   *
   * @return A pointer to the new allocated Clip object.
   */
  inline Clip* allocate_clip() {
    return (Clip*)clip_allocator.allocate();
  }

  /**
   * @brief Destroy clip. The callee must unlink the clip from the track clip list.
   *
   * @return A pointer to the new allocated Clip object.
   */
  inline void destroy_clip(Clip* clip) {
    clip->~Clip();
    clip_allocator.free(clip);
  }

  void mark_clip_deleted(Clip* clip);

  /**
   * @brief Query clips within the minimum and maximum time range.
   *
   * @param min Minimum time.
   * @param max Maximum time.
   * @return ClipQueryResult
   */
  std::optional<ClipQueryResult> query_clip_by_range(double min, double max) const;

  void update_clip_ordering();

  /**
   * @brief Find next clip at a given time position.
   *
   * @param time_pos Search starting position in beats.
   * @param hint Hint Clip ID to speed up search.
   * @return A pointer to clip id.
   */
  std::optional<uint32_t> find_next_clip(double time_pos, uint32_t hint = WB_INVALID_CLIP_ID);

  void prepare_effect_buffer(uint32_t num_channels, uint32_t num_samples);

  /**
   * @brief Reset playback state. When the playback state changes, this must be called to update the playback state and
   * make sure everything keep sync.
   *
   * @param time_pos Starting point of the playback.
   * @param refresh_voices Refresh active voices.
   */
  void reset_playback_state(double time_pos, bool refresh_voices);

  /**
   * @brief Prepare track for recording.
   *
   * @param time_pos Starting point of the recording.
   */
  void prepare_record(double time_pos);

  /**
   * @brief Stop track recording.
   */
  void stop_record();

  /**
   * @brief Stop playback of the track.
   */
  void stop();

  void process_event(
      double start_time,
      double end_time,
      double sample_position,
      double beat_duration,
      double buffer_duration,
      double sample_rate,
      double ppq,
      double inv_ppq,
      uint32_t buffer_size);

  void process_midi_event(
      Clip* clip,
      double start_time,
      double end_time,
      double sample_position,
      double beat_duration,
      double sample_rate,
      double ppq,
      double inv_ppq,
      uint32_t buffer_size);

  void kill_all_voices(uint32_t buffer_offset, double time_pos);

  /**
   * @brief Process audio block.
   *
   * @param output_buffer Position in beats.
   * @param sample_rate Sample rate.
   * @param playing Should play the track.
   */
  void process(
      const AudioBuffer<float>& input_buffer,
      AudioBuffer<float>& output_buffer,
      double sample_rate,
      double beat_duration,
      double buffer_duration_in_beats,
      double sample_position,
      double start_time,
      double end_time,
      double ppq,
      double inv_ppq,
      int64_t playhead_in_samples,
      bool playing);

  void render_sample(
      AudioBuffer<float>& output_buffer,
      float gain,
      uint32_t buffer_offset,
      uint32_t num_samples,
      double sample_rate);

  void stream_sample(
      AudioBuffer<float>& output_buffer,
      Sample* sample,
      float gain,
      uint32_t buffer_offset,
      uint32_t num_samples,
      size_t sample_offset);

  void process_test_synth(AudioBuffer<float>& output_buffer, double sample_rate, bool playing);

  void process_track_messages(double time);

  static PluginResult plugin_begin_edit(void* userdata, PluginInterface* plugin, uint32_t param_id);
  static PluginResult
  plugin_perform_edit(void* userdata, PluginInterface* plugin, uint32_t param_id, double normalized_value);
  static PluginResult plugin_end_edit(void* userdata, PluginInterface* plugin, uint32_t param_id);
};

}  // namespace wb