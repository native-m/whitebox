#pragma once

#include <string>

#include "asset.h"
#include "audio_io.h"
#include "core/audio_buffer.h"
#include "core/color.h"
#include "core/thread.h"
#include "core/timing.h"
#include "core/vector.h"
#include "etypes.h"
#include "plughost/plugin_interface.h"
#include "track_input.h"

namespace wb {

struct Track;
struct Clip;

using BpmUpdatedCallbackFn = void (*)(void* userdata, double beat_duration, double bpm);

struct Engine2 {
  static uint32_t num_input_channels;
  static uint32_t num_output_channels;
  static uint32_t audio_buffer_size;
  static uint32_t audio_sample_rate;
  static double audio_buffer_duration_ms;
  static volatile double playhead;
  static Vector<Track*> tracks;
  static PerformanceMeasurer perf_measurer;
  static AudioIO2* audio_io;
  static AudioIOType audio_io_type;
  static AudioDeviceProperties output_device_properties;
  static AudioDeviceProperties input_device_properties;
  static AudioEngineConfig current_engine_config;
  static AudioEngineConfig audio_engine_config;

  static void initialize();
  static void shutdown();
  static void clear_all();

  static void play();
  static void stop();
  static void record();
  static void stop_record();
  static void set_playhead_position(double position);
  static void set_bpm(double bpm);
  static double get_song_duration();
  static double get_beat_duration();
  static double get_bpm();
  static double get_ppq();
  static bool is_playing();
  static bool is_recording();

  static void begin_edit();
  static void end_edit();

  static void preview_sample(AudioAsset* asset);

  static Track*
  create_track(const std::string& name, const Color& color, float height, float volume_db = 0.0f, float pan = 0.0f);
  static Track*
  add_track(const std::string& name, const Color& color, float height, float volume_db = 0.0f, float pan = 0.0f);
  static void delete_track(TrackID slot);
  static void solo_track(TrackID slot);
  static void set_track_recording_state(TrackID slot, bool armed);
  static void set_track_input(Track* track, TrackInputType type, uint32_t index, bool armed);
  static void update_track_state(Track* track);

  static Clip* allocate_clip();
  static Clip*
  create_clip(const std::string& name, const Color& color, double start_pos, double end_pos, double start_offset = 0.0);
  static void destroy_clip(Clip* clip);

  static PluginInterface* add_plugin(Track* track, uint32_t slot, PluginUID uid);
  static PluginInterface* add_plugin(Track* track, PluginUID uid);
  static void remove_plugin(Track* track, uint32_t slot);

  static bool init_audio_io();
  static void shutdown_audio_io();
  static void rescan_audio_device();
  static bool start_audio_engine();
  static void stop_audio_engine();
  static void reset_audio_engine_config(bool reset_buffer_size = true);
  static bool is_audio_engine_running();

  static void
  set_processing_param(uint32_t input_channels, uint32_t output_channels, uint32_t buffer_size, uint32_t sample_rate);
  static void update_audio_visualization(float frame_rate);
  static void process(AudioBuffer<float>& output_buffer, const AudioBuffer<float>& input_buffer, double sample_rate);

  static void add_bpm_update_listener(void* userdata, BpmUpdatedCallbackFn fn);
  static void add_audio_device_removed_listener(void* userdata, AudioDeviceRemovedFn fn);
  static void add_audio_device_format_changed_listener(void* userdata, AudioDeviceFormatChangedFn fn);
};

}  // namespace wb