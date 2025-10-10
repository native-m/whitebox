#include "engine2.h"

#include <fmt/chrono.h>

#include "asset.h"
#include "audio_io.h"
#include "audio_record.h"
#include "core/queue.h"
#include "dsp/sampler.h"
#include "extern/xxhash.h"
#include "plughost/plugin_manager.h"
#include "track.h"

namespace wb {

enum class PlaybackState {
  Stop,
  Play,
  Record,
};

struct EngineMessage {
  enum {
    SetMainVolume,
    PreviewSample,
  };

  uint32_t type;

  union {
    float main_volume;
    AudioAsset* preview_sample;
  };
};

static const uint32_t audio_record_buffer_size = 64 * 1024;
static const uint32_t audio_record_file_chunk_size = 8 * 1024;
static const uint32_t audio_record_chunk_size = 256 * 1024;

static Pool<Clip> clip_allocator_;
static std::vector<TrackInputGroup> track_input_groups_;
static Vector<Pair<BpmUpdatedCallbackFn, void*>> bpm_updated_listener_;
static Vector<Pair<AudioDeviceRemovedFn, void*>> device_removed_listener_;
static Vector<Pair<AudioDeviceFormatChangedFn, void*>> device_format_changed_listener_;
static Vector<uint32_t> active_track_inputs_;
static Vector<uint32_t> active_record_tracks_;
static PlaybackState playback_state_;
static double playhead_start_;
static double sample_position_;
static double ppq_ = 96.0;

static ConcurrentRingBuffer<EngineMessage> engine_msg_queue_;
static AudioAsset* current_preview_sample;
static dsp::Sampler preview_sampler;

alignas(64) static Spinlock edit_lock_;
alignas(64) static std::atomic<uint32_t> playhead_updated_;
alignas(64) static std::atomic<double> beat_duration_;

static AudioBuffer<float> mixing_buffer_;
static AudioRecordQueue recorder_queue_;
static Vector<Sample> recorded_samples;
static std::thread record_thread_;

static Vector<Clip*> tmp_clips_;

uint32_t Engine2::num_input_channels;
uint32_t Engine2::num_output_channels;
uint32_t Engine2::audio_buffer_size;
uint32_t Engine2::audio_sample_rate;
double Engine2::audio_buffer_duration_ms;
volatile double Engine2::playhead;
Vector<Track*> Engine2::tracks;
PerformanceMeasurer Engine2::perf_measurer;
AudioIO2* Engine2::audio_io;
AudioIOType Engine2::audio_io_type;
AudioDeviceProperties Engine2::output_device_properties;
AudioDeviceProperties Engine2::input_device_properties;
AudioEngineConfig Engine2::current_engine_config;
AudioEngineConfig Engine2::audio_engine_config;

static void call_device_removed_listener(void* userdata, bool reset_audio_device);
static void call_device_format_changed_listener(void* userdata);
static void set_playback_state(PlaybackState state);
static void set_track_input(Track* track, TrackInputType type, uint32_t index, bool armed);
static void stop_recording();
static void write_recorded_samples(uint32_t num_samples);
static void record_thread_runner();

void Engine2::initialize() {
  engine_msg_queue_.set_capacity(64);
  audio_engine_config.num_output_channels = 2;
}

void Engine2::shutdown() {
  if (audio_io != nullptr || Engine2::is_audio_engine_running())
    Engine2::shutdown_audio_io();
  clear_all();
}

void Engine2::clear_all() {
  for (auto track : tracks) {
    delete track;
  }
}

void Engine2::play() {
  set_playback_state(PlaybackState::Play);
}

void Engine2::stop() {
  if (playback_state_ == PlaybackState::Record)
    stop_recording();
  set_playback_state(PlaybackState::Stop);
}

void Engine2::record() {
  if (playback_state_ == PlaybackState::Record)
    return;
  if (track_input_groups_.size() != 0) {
    recorder_queue_.start(AudioFormat::F32, audio_record_buffer_size / 4, track_input_groups_);
    record_thread_ = std::thread(record_thread_runner);
  }
  set_playback_state(PlaybackState::Record);
}

void Engine2::stop_record() {
  if (playback_state_ == PlaybackState::Record)
    stop_recording();
}

void Engine2::set_playhead_position(double position) {
  std::scoped_lock lock(edit_lock_);
  playhead_start_ = position;
  playhead = position;
}

void Engine2::set_bpm(double bpm) {
  double new_beat_duration = 60.0 / bpm;
  beat_duration_.store(new_beat_duration, std::memory_order_release);
  for (auto [cb, userdata] : bpm_updated_listener_) {
    cb(userdata, new_beat_duration, bpm);
  }
}

double Engine2::get_beat_duration() {
  return beat_duration_.load(std::memory_order_relaxed);
}

double Engine2::get_bpm() {
  return 60.0 / beat_duration_.load(std::memory_order_relaxed);
}

double Engine2::get_ppq() {
  return ppq_;
}

bool Engine2::is_playing() {
  return playback_state_ != PlaybackState::Stop;
}

bool Engine2::is_recording() {
  return playback_state_ == PlaybackState::Record;
}

void Engine2::begin_edit() {
  edit_lock_.lock();
}

void Engine2::end_edit() {
  edit_lock_.unlock();
}

void Engine2::preview_sample(AudioAsset* asset) {
  asset->add_ref();
  engine_msg_queue_.push({
    .type = EngineMessage::PreviewSample,
    .preview_sample = asset,
  });
}

Track* Engine2::create_track(const std::string& name, const Color& color, float height, float volume_db, float pan) {
  Track* track =
      new Track(name, color, height, true, TrackParameterState{ .volume_db = volume_db, .pan = pan, .mute = false });
  if (is_audio_engine_running())
    track->prepare_effect_buffer(current_engine_config.num_output_channels, current_engine_config.buffer_size);
  std::scoped_lock lock(edit_lock_);
  tracks.push_back(track);
  return track;
}

void Engine2::delete_track(uint32_t slot) {
  Track* track = tracks[slot];
  std::scoped_lock lock(edit_lock_);
  if (track->input.type != TrackInputType::None)
    set_track_input(track, TrackInputType::None, 0, false);
  tracks.erase_at(slot);
  delete track;
}

void Engine2::solo_track(uint32_t slot) {
  bool mute = false;
  if (tracks[slot]->ui_parameter_state.solo) {
    tracks[slot]->ui_parameter_state.solo = false;
  } else {
    tracks[slot]->ui_parameter_state.solo = true;
    tracks[slot]->set_mute(false);
    mute = true;
  }

  for (uint32_t i = 0; i < tracks.size(); i++) {
    if (i == slot)
      continue;
    if (tracks[i]->ui_parameter_state.solo)
      tracks[i]->ui_parameter_state.solo = false;
    tracks[i]->set_mute(mute);
  }
}

void Engine2::set_track_recording_state(uint32_t slot, bool armed) {
  Track* track = tracks[slot];
  set_track_input(track, track->input.type, track->input.index, armed);
}

void Engine2::set_track_input(Track* track, TrackInputType type, uint32_t index, bool armed) {
  uint32_t new_input = TrackInput{ type, index }.as_packed_u32();
  uint32_t old_input = track->input.as_packed_u32();
  auto new_pred = [new_input](const TrackInputGroup& x) { return x.input == new_input; };
  auto old_pred = [old_input](const TrackInputGroup& x) { return x.input == old_input; };
  track->input_attr.armed = armed;

  if (armed && (track->input.type != type || track->input.index != index)) {
    // Remove previous input assignment
    auto input_map = std::find_if(track_input_groups_.begin(), track_input_groups_.end(), old_pred);
    if (input_map != track_input_groups_.end() && input_map->input_attrs == &track->input_attr) {
      input_map->input_attrs = track->input_attr.next();
      if (input_map->input_attrs == nullptr)
        track_input_groups_.erase(input_map);
    }
    track->input_attr.remove_from_list();
    // Assign new input
    if (type != TrackInputType::None) {
      input_map = std::find_if(track_input_groups_.begin(), track_input_groups_.end(), new_pred);
      if (input_map == track_input_groups_.end()) {
        track_input_groups_.emplace_back(new_input, &track->input_attr);
      } else {
        input_map->input_attrs->push_item_front(&track->input_attr);
        input_map->input_attrs = &track->input_attr;
      }
    }
  } else {
    auto input_map = std::find_if(track_input_groups_.begin(), track_input_groups_.end(), new_pred);
    if (armed && type != TrackInputType::None) {
      // Assign new input
      if (input_map == track_input_groups_.end()) {
        track_input_groups_.emplace_back(new_input, &track->input_attr);
      } else if (track->input.type != type || track->input.index != index) {
        input_map->input_attrs->push_item_front(&track->input_attr);
        input_map->input_attrs = &track->input_attr;
      }
    } else {
      // Remove input assignment if not armed
      if (input_map != track_input_groups_.end() && input_map->input_attrs == &track->input_attr) {
        input_map->input_attrs = track->input_attr.next();
        if (input_map->input_attrs == nullptr)
          track_input_groups_.erase(input_map);
      }
      track->input_attr.remove_from_list();
    }
  }

  track->input.type = type;
  track->input.index = index;
}

void Engine2::update_track_state(Track* track) {
  /*Vector<Clip*> new_cliplist;
  new_cliplist.reserve(track->clips.capacity());

  if (track->has_deleted_clips) {
    for (auto clip : track->clips) {
      if (clip->is_deleted()) {
        tmp_clips_.push_back(clip);
        continue;
      }
      new_cliplist.push_back(clip);
    }
    track->has_deleted_clips = false;
    track->clips = std::move(new_cliplist);
    for (auto clip : tmp_clips_) {
      clip->~Clip();
      clip_allocator_.free(clip);
    }
    tmp_clips_.resize(0);
  }*/

  std::sort(
      track->clips.begin(), track->clips.end(), [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });

  for (uint32_t i = 0; i < (uint32_t)track->clips.size(); i++) {
    track->clips[i]->id = i;
  }
}

Clip* Engine2::allocate_clip() {
  return (Clip*)clip_allocator_.allocate();
}

Clip* Engine2::create_clip(
    const std::string& name,
    const Color& color,
    double start_pos,
    double end_pos,
    double start_offset) {
  Clip* clip = (Clip*)clip_allocator_.allocate();
  if (!clip)
    return nullptr;
  return new (clip) Clip(name, color, start_pos, end_pos, start_offset);
}

void Engine2::destroy_clip(Clip* clip) {
  clip->~Clip();
  clip_allocator_.free(clip);
}

PluginInterface* Engine2::add_plugin(Track* track, uint32_t slot, PluginUID uid) {
  return nullptr;
}

PluginInterface* Engine2::add_plugin(Track* track, PluginUID uid) {
  PluginInterface* plugin = pm_open_plugin(uid);
  if (!plugin) {
    Log::error("Failed to open plugin");
    return nullptr;
  }

  if (WB_PLUG_FAIL(plugin->init())) {
    plugin->shutdown();
    pm_close_plugin(plugin);
    Log::error("Failed to initialize plugin");
    return nullptr;
  }

  plugin->set_handler(&track->plugin_handler, track);

  uint32_t input_audio_bus_count = plugin->get_audio_bus_count(false);
  uint32_t output_audio_bus_count = plugin->get_audio_bus_count(true);
  uint32_t input_event_bus_count = plugin->get_event_bus_count(false);
  uint32_t default_input_bus = 0;
  uint32_t default_output_bus = 0;

  Log::debug("---- Plugin audio input bus ----");
  for (uint32_t i = 0; i < input_audio_bus_count; i++) {
    PluginAudioBusInfo bus_info;
    plugin->get_audio_bus_info(false, i, &bus_info);
    Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
    Log::debug("\tChannel count: {}", bus_info.channel_count);
    Log::debug("\tDefault bus: {}", bus_info.default_bus);
    if (bus_info.default_bus) {
      if (WB_PLUG_FAIL(plugin->activate_audio_bus(false, i, true)))
        Log::error("Failed to open audio input bus {}", i);
      default_input_bus = i;
    }
  }

  Log::debug("---- Plugin audio output bus ----");
  for (uint32_t i = 0; i < output_audio_bus_count; i++) {
    PluginAudioBusInfo bus_info;
    plugin->get_audio_bus_info(true, i, &bus_info);
    Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
    Log::debug("\tChannel count: {}", bus_info.channel_count);
    Log::debug("\tDefault bus: {}", bus_info.default_bus);
    if (bus_info.default_bus) {
      if (WB_PLUG_FAIL(plugin->activate_audio_bus(true, i, true)))
        Log::error("Failed to open audio input bus {}", i);
      default_output_bus = i;
    }
  }

  Log::debug("---- Plugin event input bus ----");
  for (uint32_t i = 0; i < input_event_bus_count; i++) {
    PluginEventBusInfo bus_info;
    plugin->get_event_bus_info(false, i, &bus_info);
    Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
    if (WB_PLUG_FAIL(plugin->activate_event_bus(false, i, true)))
      Log::error("Failed to open audio input bus {}", i);
  }

  if (is_audio_engine_running()) {
    uint32_t buffer_size = current_engine_config.buffer_size;
    double sample_rate = (double)audio_sample_rate;
    if (WB_PLUG_FAIL(
            plugin->init_processing(PluginProcessingMode::Realtime, current_engine_config.buffer_size, sample_rate))) {
      Log::error("Cannot initialize processing");
    }

    if (WB_PLUG_FAIL(plugin->start_processing()))
      Log::error("Cannot start plugin processing");
  }

  edit_lock_.lock();
  track->default_input_bus = default_input_bus;
  track->default_output_bus = default_output_bus;
  track->plugin_instance = plugin;
  edit_lock_.unlock();
  return plugin;
}

void Engine2::remove_plugin(Track* track, uint32_t slot) {
  if (track->plugin_instance) {
    PluginInterface* plugin = track->plugin_instance;
    edit_lock_.lock();
    track->plugin_instance = nullptr;
    edit_lock_.unlock();
    plugin->stop_processing();
    plugin->shutdown();
    pm_close_plugin(plugin);
  }
}

bool Engine2::init_audio_io() {
  if (audio_io)
    return false;  // Already initialized
  audio_io = AudioIO2::create(audio_io_type);
  if (audio_io) {
    audio_io->set_device_removed_listener(nullptr, call_device_removed_listener);
    audio_io->set_device_format_changed_listener(nullptr, call_device_format_changed_listener);
  }
  return audio_io != nullptr;
}

void Engine2::shutdown_audio_io() {
  if (!audio_io)
    return;
  if (audio_io->is_device_open())
    stop_audio_engine();
  delete audio_io;
  audio_io = nullptr;
}

void Engine2::rescan_audio_device() {
  audio_io->rescan_device();
}

bool Engine2::start_audio_engine() {
  if (!audio_io)
    return false;

  uint32_t input_device_idx = audio_io->get_input_device_index(audio_engine_config.input_device_id);
  uint32_t output_device_idx = audio_io->get_output_device_index(audio_engine_config.output_device_id);

  input_device_properties = input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                ? audio_io->get_input_device_properties(input_device_idx)
                                : audio_io->default_input_device;
  output_device_properties = output_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                 ? audio_io->get_output_device_properties(output_device_idx)
                                 : audio_io->default_output_device;

  // Override invalid input/output device
  if (input_device_properties.id != audio_engine_config.input_device_id) {
    audio_engine_config.input_device_id = input_device_properties.id;
    input_device_idx = audio_io->get_input_device_index(input_device_properties.id);
  }

  if (output_device_properties.id != audio_engine_config.output_device_id) {
    audio_engine_config.output_device_id = output_device_properties.id;
    output_device_idx = audio_io->get_output_device_index(output_device_properties.id);
  }

  if (!audio_io->open_device(input_device_idx, output_device_idx)) {
    Log::error("Cannot open audio device");
    return false;
  }

  audio_sample_rate = get_sample_rate_value(audio_engine_config.sample_rate);
  AudioDevicePeriod period = buffer_size_to_period(audio_engine_config.buffer_size, audio_sample_rate);
  uint32_t buffer_size = audio_engine_config.buffer_size;

  // Clamp buffer size
  if (period < audio_io->min_period)
    buffer_size = period_to_buffer_size(audio_io->min_period, audio_sample_rate);

  // Realign buffer size
  buffer_size -= buffer_size % audio_io->buffer_alignment;

  audio_buffer_duration_ms = period_to_ms(period);
  audio_engine_config.buffer_size = buffer_size;
  audio_engine_config.num_input_channels = audio_io->max_input_channel_count;
  audio_engine_config.num_output_channels = audio_io->max_output_channel_count;
  audio_engine_config.priority = AudioThreadPriority::Highest;
  mixing_buffer_.resize(audio_engine_config.buffer_size, true);
  mixing_buffer_.resize_channel(audio_engine_config.num_output_channels);

  for (auto track : tracks)
    track->prepare_effect_buffer(audio_engine_config.num_output_channels, audio_engine_config.buffer_size);

  // TODO(native-m): Sample rate, sample formats and channel numbers should be user-defined
  if (!audio_engine_config.exclusive_mode) {
    audio_engine_config.sample_rate = audio_io->shared_mode_sample_rate;
    audio_engine_config.input_format = audio_io->shared_mode_output_format;
    audio_engine_config.output_format = audio_io->shared_mode_input_format;
  }

  current_engine_config = audio_engine_config;

  bool success = audio_io->start(
      audio_engine_config.exclusive_mode,
      audio_engine_config.buffer_size,
      audio_engine_config.sample_rate,
      audio_engine_config.input_format,
      audio_engine_config.output_format,
      audio_engine_config.num_input_channels,
      audio_engine_config.num_output_channels,
      audio_engine_config.priority,
      process);

  if (!success) {
    Log::error("Cannot start audio stream");
    return false;
  }

  return true;
}

void Engine2::stop_audio_engine() {
  if (audio_io && audio_io->is_device_open()) {
    audio_io->close_device();
  }
}

void Engine2::reset_audio_engine_config(bool reset_buffer_size) {
  AudioIOType default_io_type = AudioIO2::get_platform_recommended_audio_io_type();

  if (audio_io && default_io_type != audio_io_type) {
    shutdown_audio_io();
  }

  bool temp_init = false;
  if (audio_io == nullptr) {
    audio_io_type = default_io_type;
    temp_init = true;
    if (!init_audio_io()) {
      Log::error("Cannot initialize audio I/O");
      return;
    }
  }

  bool restart_audio_engine = false;

  // Suspend the audio engine
  if (audio_io->is_device_open()) {
    restart_audio_engine = true;
    stop_audio_engine();
  }

  AudioDeviceProperties input_device_props = audio_io->default_input_device;
  AudioDeviceProperties output_device_props = audio_io->default_output_device;
  uint32_t input_device_idx = audio_io->get_input_device_index(input_device_props.id);
  uint32_t output_device_idx = audio_io->get_output_device_index(output_device_props.id);
  AudioEngineConfig config;

  if (!audio_io->open_device(input_device_idx, output_device_idx)) {
    Log::error("Cannot open audio device");
    return;
  }

  static constexpr uint32_t default_buffer_size = 512;
  AudioDeviceSampleRate sample_rate = audio_io->shared_mode_sample_rate;
  AudioFormat output_sample_format = audio_io->shared_mode_output_format;
  AudioFormat input_sample_format = audio_io->shared_mode_input_format;
  uint32_t sample_rate_value = get_sample_rate_value(sample_rate);
  uint32_t buffer_size = reset_buffer_size ? default_buffer_size : current_engine_config.buffer_size;

  if (audio_io->min_period > buffer_size_to_period(default_buffer_size, sample_rate_value)) {
    buffer_size = period_to_buffer_size(audio_io->min_period, sample_rate_value);
  } else {
    buffer_size = default_buffer_size;
  }

  audio_engine_config.input_device_id = input_device_props.id;
  audio_engine_config.output_device_id = output_device_props.id;
  audio_engine_config.buffer_size = buffer_size;
  audio_engine_config.num_input_channels = audio_io->max_input_channel_count;
  audio_engine_config.num_output_channels = audio_io->max_output_channel_count;
  audio_engine_config.sample_rate = sample_rate;
  audio_engine_config.input_format = input_sample_format;
  audio_engine_config.output_format = output_sample_format;
  audio_engine_config.priority = AudioThreadPriority::High;
  audio_engine_config.exclusive_mode = audio_io->exclusive_mode_support && !audio_io->shared_mode_support;

  audio_io->close_device();

  if (restart_audio_engine)
    start_audio_engine();
  else if (temp_init)
    shutdown_audio_io();
}

bool Engine2::is_audio_engine_running() {
  return audio_io && audio_io->is_stream_running();
}

void Engine2::set_processing_param(
    uint32_t input_channels,
    uint32_t output_channels,
    uint32_t buffer_size,
    uint32_t sample_rate) {
  num_input_channels = input_channels;
  num_output_channels = output_channels;
  audio_buffer_size = buffer_size;
  audio_sample_rate = sample_rate;
  audio_buffer_duration_ms = period_to_ms(buffer_size_to_period(buffer_size, sample_rate));
  mixing_buffer_.resize(buffer_size);
  mixing_buffer_.resize_channel(output_channels);
  for (auto track : tracks)
    track->prepare_effect_buffer(num_output_channels, buffer_size);
}

void Engine2::update_audio_visualization(float frame_rate) {
  double frame_rate_sec = 1.0 / (double)frame_rate;
  double buffer_duration_sec = audio_buffer_duration_ms / 1000.0;
  double speed = (double)frame_rate * std::max(frame_rate_sec, buffer_duration_sec);
  for (auto track : tracks) {
    for (auto& vu_channel : track->level_meter) {
      vu_channel.update(frame_rate, (float)(speed * 0.1));
    }
  }
}

void Engine2::process(AudioBuffer<float>& output_buffer, const AudioBuffer<float>& input_buffer, double sample_rate) {
  ScopedPerformanceCounter counter;
  {
    double inv_ppq = 1.0 / ppq_;
    double current_playhead_position = playhead;
    double current_beat_duration = beat_duration_.load(std::memory_order_relaxed);
    double buffer_duration = (double)output_buffer.n_samples / sample_rate;
    double buffer_duration_in_beats = buffer_duration / current_beat_duration;
    double next_playhead_pos = playhead + buffer_duration_in_beats;
    int64_t playhead_in_samples = beat_to_samples(playhead, sample_rate, current_beat_duration);
    std::scoped_lock lock(edit_lock_);
    PlaybackState state = playback_state_;
    bool is_playing = state != PlaybackState::Stop;

    output_buffer.clear();

    EngineMessage msg;
    while (engine_msg_queue_.pop(msg)) {
      switch (msg.type) {
        case EngineMessage::SetMainVolume: break;
        case EngineMessage::PreviewSample: {
          if (is_playing)
            break;
          if (current_preview_sample) {
            current_preview_sample->release();
          }
          current_preview_sample = msg.preview_sample;
          preview_sampler.reset_state(
              dsp::ResamplerType::Linear, 0.0, 1.0, msg.preview_sample->sample.sample_rate, sample_rate);
          break;
        }
      }
    }

    // Process all tracks
    for (uint32_t i = 0; i < tracks.size(); i++) {
      auto track = tracks[i];
      track->audio_event_buffer.resize(0);
      track->midi_event_list.clear();

      if (track->midi_voice_state.has_voice() && !is_playing) {
        track->kill_all_voices(0, playhead);
      }

      mixing_buffer_.clear();
      track->process(
          input_buffer,
          mixing_buffer_,
          sample_rate,
          current_beat_duration,
          buffer_duration_in_beats,
          sample_position_,
          current_playhead_position,
          next_playhead_pos,
          ppq_,
          inv_ppq,
          playhead_in_samples,
          is_playing);

      output_buffer.mix(mixing_buffer_);
    }

    if (is_playing) {
      sample_position_ += beat_to_samples(buffer_duration_in_beats, sample_rate, current_beat_duration);
      playhead = next_playhead_pos;

      if (current_preview_sample) {
        current_preview_sample->release();
        current_preview_sample = nullptr;
      }

      if (state == PlaybackState::Record) {
        recorder_queue_.begin_write(audio_buffer_size);
        for (uint32_t i = 0; i < track_input_groups_.size(); i++) {
          TrackInput input = TrackInput::from_packed_u32(track_input_groups_[i].input);
          switch (input.type) {
            case TrackInputType::ExternalStereo: recorder_queue_.write(i, input.index * 2, 2, input_buffer); break;
            case TrackInputType::ExternalMono: recorder_queue_.write(i, input.index, 1, input_buffer); break;
            default: WB_UNREACHABLE();
          }
        }
        recorder_queue_.end_write();
      }
    }

    if (current_preview_sample) {
      Sample* sample = &current_preview_sample->sample;
      if (!preview_sampler.stream(
              sample, output_buffer.n_channels, output_buffer.n_samples, 0, 0.75f, output_buffer.channel_buffers)) {
        // Has finished playing
        current_preview_sample->release();
        current_preview_sample = nullptr;
      }
    }

    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
      float* channel = output_buffer.get_write_pointer(i);
      for (uint32_t j = 0; j < output_buffer.n_samples; j++) {
        if (channel[j] > 1.0) {
          channel[j] = 1.0;
        } else if (channel[j] < -1.0) {
          channel[j] = -1.0;
        }
      }
    }
  }
  perf_measurer.update(tm_ticks_to_ms(counter.duration()), audio_buffer_duration_ms);
}

void Engine2::add_bpm_update_listener(void* userdata, BpmUpdatedCallbackFn fn) {
  bpm_updated_listener_.emplace_back(fn, userdata);
}

void Engine2::add_audio_device_removed_listener(void* userdata, AudioDeviceRemovedFn fn) {
  device_removed_listener_.emplace_back(fn, userdata);
}

void Engine2::add_audio_device_format_changed_listener(void* userdata, AudioDeviceFormatChangedFn fn) {
  device_format_changed_listener_.emplace_back(fn, userdata);
}

void call_device_removed_listener(void* internal_userdata, bool reset_audio_device) {
  for (auto [fn, userdata] : device_removed_listener_) {
    fn(userdata, reset_audio_device);
  }
}

void call_device_format_changed_listener(void* internal_userdata) {
  for (auto [fn, userdata] : device_format_changed_listener_) {
    fn(userdata);
  }
}

void set_playback_state(PlaybackState state) {
  std::scoped_lock lock(edit_lock_);

  if (state == PlaybackState::Stop) {
    playback_state_ = state;
    Engine2::playhead = playhead_start_;
    for (auto track : Engine2::tracks)
      track->stop();
    return;
  }

  if (playback_state_ == PlaybackState::Stop) {
    for (auto track : Engine2::tracks) {
      if (state == PlaybackState::Record)
        track->prepare_record(playhead_start_);
      track->reset_playback_state(playhead_start_, false);
    }
    sample_position_ = 0.0;
  }

  playback_state_ = state;
}

void stop_recording() {
  if (track_input_groups_.size() != 0) {
    recorder_queue_.stop();
    record_thread_.join();
  }
  for (auto track : Engine2::tracks) {
    if (track->input_attr.recording) {
      std::string name;
      auto current_datetime = std::chrono::system_clock::now();
      // Set sample name
      fmt::format_to(std::back_inserter(name), "{} - {}", current_datetime, track->name);
      std::replace(name.begin(), name.end(), ':', '_');  // Path does not support colon
      track->recorded_samples->name = std::move(name);
      track->recorded_samples->path = track->recorded_samples->name;
      // Adjust the sample count to the actual number of samples written
      track->recorded_samples->resize(track->num_samples_written, track->recorded_samples->channels);
      track->num_samples_written = 0;  // Reset back to zero
      // Transform the recorded sample into asset and create the audio clip
      SampleAsset* asset = g_sample_table.create_from_existing_sample(std::move(*track->recorded_samples));
      /*add_audio_clip(
          track,
          asset->sample_instance.name,
          track->record_min_time,
          track->record_max_time,
          0.0,
          AudioClip{ .asset = asset, .speed = 1.0, .gain = 1.0f });*/
      track->recorded_samples.reset();
    }
    track->stop_record();
  }
}

void write_recorded_samples(uint32_t num_samples) {
  for (uint32_t i = 0; i < track_input_groups_.size(); i++) {
    TrackInputGroup& group = track_input_groups_[i];
    TrackInput input = TrackInput::from_packed_u32(group.input);
    uint32_t num_channels = input.type == TrackInputType::ExternalMono ? 1 : 2;
    for (auto input_attr = group.input_attrs; input_attr != nullptr; input_attr = input_attr->next()) {
      Track* track = input_attr->track;
      size_t required_size = track->num_samples_written + num_samples;
      if (!track->recorded_samples) {
        // Create new sample instance if not exist
        track->recorded_samples.emplace(AudioFormat::F32, Engine2::audio_sample_rate);
        track->recorded_samples->resize(audio_record_chunk_size / 4, num_channels);
      } else if (required_size >= track->recorded_samples->count) {
        // Resize the storage size if the required size exceeds current sample count
        track->recorded_samples->resize(track->recorded_samples->count + audio_record_chunk_size / 4, num_channels);
        Log::debug("Resize sample");
      }
      auto sample_data = track->recorded_samples->get_sample_data<float>();
      recorder_queue_.read(i, sample_data, track->num_samples_written, 0, num_channels);
      track->num_samples_written = required_size;
    }
  }
}

void record_thread_runner() {
  uint32_t num_samples_to_read = audio_record_file_chunk_size / 4;
  while (recorder_queue_.begin_read(num_samples_to_read)) {
    write_recorded_samples(num_samples_to_read);
    recorder_queue_.end_read();
  }
  uint32_t remaining_samples = recorder_queue_.size();
  if (remaining_samples > 0) {
    recorder_queue_.begin_read(remaining_samples);
    write_recorded_samples(remaining_samples);
    recorder_queue_.end_read();
  }
}

}  // namespace wb