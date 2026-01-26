#include "audio_io.h"

#ifdef WB_PLATFORM_LINUX
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>
#include <spa/utils/ringbuffer.h>
#include <spa/utils/hook.h>
#include <spa/utils/dict.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/utils/defs.h>
#include <atomic>
#include <cstring>
#include <optional>

#include "core/debug.h"
#include "core/vector.h"
#include "extern/xxhash.h"

namespace wb {

struct AudioIOPipeWire2;

struct AudioDevicePipeWire2 {
  uint32_t index = 0;
  uint32_t pw_id = 0;
  AudioDeviceProperties properties{};

  char node_name[256]{};

  AudioDevicePipeWire2() = default;
};

struct ActiveDevicePipeWire2 {
  pw_stream* stream{};
  spa_hook stream_listener{};
  AudioFormat sample_format{};
  uint32_t sample_rate{};
  uint32_t num_channels{};
  uint32_t buffer_size{};
  bool is_input{};
  AudioIOPipeWire2* io{};

  bool open(
      AudioIOPipeWire2* parent_io,
      pw_core* core,
      pw_thread_loop* thread_loop,
      char node_name[256],
      bool is_input_device,
      AudioFormat format,
      uint32_t rate,
      uint32_t channels,
      uint32_t buf_size);
  void close();
  bool start();
  void stop();
};

struct RegistryEventsPipeWire {
  AudioIOPipeWire2* io;
  spa_hook registry_listener{};

  RegistryEventsPipeWire(AudioIOPipeWire2* audio_io);
};

struct AudioIOPipeWire2 final : public AudioIO2 {
  Vector<AudioDevicePipeWire2> input_devices;
  Vector<AudioDevicePipeWire2> output_devices;

  pw_thread_loop* thread_loop{};
  pw_context* context{};
  pw_core* core{};
  pw_registry* registry{};
  RegistryEventsPipeWire registry_events{ this };

  std::optional<AudioDeviceID> active_input_device_id;
  std::optional<AudioDeviceID> active_output_device_id;

  ActiveDevicePipeWire2 input;
  ActiveDevicePipeWire2 output;

  AudioDevicePeriod stream_period{};
  uint32_t stream_buffer_size{};
  double stream_sample_rate{};
  std::atomic_bool running{};
  AudioStreamFn stream_fn{};

  AudioBuffer<float> input_buffer;
  AudioBuffer<float> output_buffer;

  ~AudioIOPipeWire2();
  bool init();
  bool scan_devices();
  uint32_t find_device_index(AudioDeviceType type, AudioDeviceID id) const;

  bool rescan_device() override;
  uint32_t get_input_device_index(AudioDeviceID id) const override;
  uint32_t get_output_device_index(AudioDeviceID id) const override;
  const AudioDeviceProperties& get_input_device_properties(uint32_t device_idx) const override;
  const AudioDeviceProperties& get_output_device_properties(uint32_t device_idx) const override;
  bool is_stream_running() const override;
  bool is_on_the_same_driver(
      AudioDeviceType a_type,
      uint32_t a_device,
      AudioDeviceType b_type,
      uint32_t b_device) const override;

  bool open_device(uint32_t input_device_idx, uint32_t output_device_idx) override;
  void close_device() override;
  bool start(
      bool exclusive_mode,
      uint32_t buffer_size,
      AudioDeviceSampleRate sample_rate,
      AudioFormat input_format,
      AudioFormat output_format,
      uint32_t num_input_channels,
      uint32_t num_output_channels,
      AudioThreadPriority priority,
      AudioStreamFn stream_callback_fn) override;

  static void on_process_input(void* userdata);
  static void on_process_output(void* userdata);
};

inline static AudioDeviceID make_device_id_hash(uint32_t pw_id, const char* name) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%u:%s", pw_id, name);
  return XXH3_64bits(buffer, strlen(buffer));
}

inline static spa_audio_format get_spa_format(AudioFormat format) {
  switch (format) {
    case AudioFormat::I16: return SPA_AUDIO_FORMAT_S16;
    case AudioFormat::I24: return SPA_AUDIO_FORMAT_S24;
    case AudioFormat::I32: return SPA_AUDIO_FORMAT_S32;
    case AudioFormat::F32: return SPA_AUDIO_FORMAT_F32;
    default: return SPA_AUDIO_FORMAT_UNKNOWN;
  }
}

bool ActiveDevicePipeWire2::open(
    AudioIOPipeWire2* parent_io,
    pw_core* core,
    pw_thread_loop* thread_loop,
    char node_name[256],
    bool is_input_device,
    AudioFormat format,
    uint32_t rate,
    uint32_t channels,
    uint32_t buf_size) {

  if (!thread_loop || !core || !node_name) return false;

  pw_loop* loop = pw_thread_loop_get_loop(thread_loop);
  if (!loop) return false;

  io = parent_io;
  is_input = is_input_device;
  sample_format = format;
  sample_rate = rate;
  num_channels = channels;
  buffer_size = buf_size;

  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, is_input ? "Capture" : "Playback",
      PW_KEY_MEDIA_ROLE, "Music",
      PW_KEY_NODE_NAME, node_name,
      nullptr);

  if (!props) return false;

  stream = pw_stream_new(
      core,
      is_input ? "Whitebox Input" : "Whitebox Output",
      props
  );

  if (!stream)
    return false;

  static const pw_stream_events stream_events_input = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = AudioIOPipeWire2::on_process_input,
  };

  static const pw_stream_events stream_events_output = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = AudioIOPipeWire2::on_process_output,
  };

  pw_stream_add_listener(stream, &stream_listener,
      is_input ? &stream_events_input : &stream_events_output, this);

  return true;
}

void ActiveDevicePipeWire2::close() {
  if (stream) {
    pw_stream_destroy(stream);
    stream = nullptr;
  }
}

bool ActiveDevicePipeWire2::start() {
  if (!stream)
    return false;

  const spa_pod* params[1];
  uint8_t buffer[1024];
  spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  spa_audio_info_raw info = {};
  info.format = get_spa_format(sample_format);
  info.rate = sample_rate;
  info.channels = num_channels;

  for (uint32_t i = 0; i < num_channels && i < SPA_AUDIO_MAX_CHANNELS; i++) {
    info.position[i] = (spa_audio_channel)(SPA_AUDIO_CHANNEL_START_Aux + i);
  }

  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  pw_stream_flags flags =
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);

  enum pw_direction direction = is_input ? SPA_DIRECTION_INPUT : SPA_DIRECTION_OUTPUT;

  pw_stream_connect(
      stream,
      direction,
      PW_ID_ANY,
      flags,
      params,
      1
  );

  return true;
}

void ActiveDevicePipeWire2::stop() {
  if (stream)
    pw_stream_disconnect(stream);
}

RegistryEventsPipeWire::RegistryEventsPipeWire(AudioIOPipeWire2* audio_io) : io(audio_io) {
}

static void on_registry_global(
    void* data,
    uint32_t id,
    uint32_t permissions,
    const char* type,
    uint32_t version,
    const struct spa_dict* props) {

  AudioIOPipeWire2* io = static_cast<AudioIOPipeWire2*>(data);

  if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
    return;

  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (!media_class)
    return;

  bool is_input = strcmp(media_class, "Audio/Source") == 0;
  bool is_output = strcmp(media_class, "Audio/Sink") == 0;

  if (!is_input && !is_output)
    return;

  const char* node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  const char* node_desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);

  if (!node_name)
    node_name = node_desc ? node_desc : "Unknown";

  AudioDevicePipeWire2 device;
  device.pw_id = id;
  device.index = is_input ? io->input_devices.size() : io->output_devices.size();
  device.properties.id = make_device_id_hash(id, node_name);
  device.properties.type = is_input ? AudioDeviceType::Input : AudioDeviceType::Output;
  device.properties.io_type = AudioIOType::PipeWire;

  strncpy(device.node_name, node_name, sizeof(device.node_name) - 1);
  device.node_name[sizeof(device.node_name) - 1] = '\0';

  snprintf(device.properties.name, sizeof(device.properties.name), "%s",
           node_desc ? node_desc : node_name);

  if (is_input)
    io->input_devices.push_back(device);
  else
    io->output_devices.push_back(device);
}

static void on_registry_global_remove(void* data, uint32_t id) {
  AudioIOPipeWire2* io = static_cast<AudioIOPipeWire2*>(data);

  auto remove_from = [id](Vector<AudioDevicePipeWire2>& devices) {
    for (size_t i = 0; i < devices.size(); i++) {
      if (devices[i].pw_id == id) {
        devices.erase(devices.begin() + i);
        return true;
      }
    }
    return false;
  };

  remove_from(io->input_devices);
  remove_from(io->output_devices);
}

AudioIOPipeWire2::~AudioIOPipeWire2() {
  close_device();

  if (thread_loop)
    pw_thread_loop_lock(thread_loop);

  if (registry) {
    spa_hook_remove(&registry_events.registry_listener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
    registry = nullptr;
  }

  if (core) {
    pw_core_disconnect(core);
    core = nullptr;
  }

  if (thread_loop)
    pw_thread_loop_unlock(thread_loop);

  if (context) {
    pw_context_destroy(context);
    context = nullptr;
  }

  if (thread_loop) {
    pw_thread_loop_destroy(thread_loop);
    thread_loop = nullptr;
  }

  pw_deinit();
}

bool AudioIOPipeWire2::init() {
  pw_init(nullptr, nullptr);

  thread_loop = pw_thread_loop_new("whitebox-pw", nullptr);
  if (!thread_loop)
    return false;

  context = pw_context_new(pw_thread_loop_get_loop(thread_loop), nullptr, 0);
  if (!context)
    return false;

  if (pw_thread_loop_start(thread_loop) < 0)
    return false;

  pw_thread_loop_lock(thread_loop);

  core = pw_context_connect(context, nullptr, 0);
  if (!core) {
    pw_thread_loop_unlock(thread_loop);
    return false;
  }

  registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  static const pw_registry_events events = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
  };

  pw_registry_add_listener(registry, &registry_events.registry_listener, &events, this);

  pw_thread_loop_unlock(thread_loop);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  shared_mode_support = true;
  exclusive_mode_support = false;

  shared_mode_input_format = AudioFormat::F32;
  shared_mode_output_format = AudioFormat::F32;
  shared_mode_sample_rate = AudioDeviceSampleRate::Hz48000;

  for (const auto& [rate_val, rate_enum] : compatible_sample_rates) {
    exclusive_sample_rate_bit_flags |= (1U << (uint32_t)rate_enum);
    exclusive_input_sample_rate_bit_flags |= (1U << (uint32_t)rate_enum);
    exclusive_output_sample_rate_bit_flags |= (1U << (uint32_t)rate_enum);
  }

  for (const auto& format : compatible_formats) {
    exclusive_input_format_bit_flags |= (1U << (uint32_t)format);
    exclusive_output_format_bit_flags |= (1U << (uint32_t)format);
  }

  max_input_channel_count = 2;
  max_output_channel_count = 2;
  min_period = 128;
  buffer_alignment = 1;

  return rescan_device();
}

bool AudioIOPipeWire2::scan_devices() {
  return rescan_device();
}

uint32_t AudioIOPipeWire2::find_device_index(AudioDeviceType type, AudioDeviceID id) const {
  const Vector<AudioDevicePipeWire2>& devices =
      (type == AudioDeviceType::Input) ? input_devices : output_devices;

  for (uint32_t i = 0; i < devices.size(); i++) {
    if (devices[i].properties.id == id)
      return i;
  }

  return WB_INVALID_AUDIO_DEVICE_INDEX;
}

bool AudioIOPipeWire2::rescan_device() {
  pw_thread_loop_lock(thread_loop);

  num_input_device = input_devices.size();
  num_output_device = output_devices.size();

  if (!input_devices.empty())
    default_input_device = input_devices[0].properties;

  if (!output_devices.empty())
    default_output_device = output_devices[0].properties;

  pw_thread_loop_unlock(thread_loop);

  return true;
}

uint32_t AudioIOPipeWire2::get_input_device_index(AudioDeviceID id) const {
  return find_device_index(AudioDeviceType::Input, id);
}

uint32_t AudioIOPipeWire2::get_output_device_index(AudioDeviceID id) const {
  return find_device_index(AudioDeviceType::Output, id);
}

const AudioDeviceProperties& AudioIOPipeWire2::get_input_device_properties(uint32_t device_idx) const {
  return input_devices[device_idx].properties;
}

const AudioDeviceProperties& AudioIOPipeWire2::get_output_device_properties(uint32_t device_idx) const {
  return output_devices[device_idx].properties;
}

bool AudioIOPipeWire2::is_stream_running() const {
  return running.load(std::memory_order_relaxed);
}

bool AudioIOPipeWire2::is_on_the_same_driver(
    AudioDeviceType a_type,
    uint32_t a_device,
    AudioDeviceType b_type,
    uint32_t b_device) const {
  return false;
}

bool AudioIOPipeWire2::open_device(uint32_t input_device_idx, uint32_t output_device_idx) {
  if (output_device_idx == WB_INVALID_AUDIO_DEVICE_INDEX)
    return false;

  if (output_device_idx >= output_devices.size()) {
    return false;
  }
  const AudioDevicePipeWire2& output_device = output_devices[output_device_idx];
  current_output_device_id = output_device.properties.id;

  if (input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX) {
    if (input_device_idx >= input_devices.size())
      return false;

    const AudioDevicePipeWire2& input_device = input_devices[input_device_idx];
    current_input_device_id = input_device.properties.id;
  }

  open = true;
  return true;
}

void AudioIOPipeWire2::close_device() {
  if (!open)
    return;

  if (running) {
    running = false;

    pw_thread_loop_lock(thread_loop);
    input.stop();
    output.stop();
    input.close();
    output.close();
    pw_thread_loop_unlock(thread_loop);
  }

  open = false;
  current_input_format = {};
  current_output_format = {};
}

bool AudioIOPipeWire2::start(
    bool exclusive_mode,
    uint32_t buffer_size,
    AudioDeviceSampleRate sample_rate,
    AudioFormat input_format,
    AudioFormat output_format,
    uint32_t num_input_channels,
    uint32_t num_output_channels,
    AudioThreadPriority priority,
    AudioStreamFn stream_callback_fn) {

  if (running)
    return false;

  if (!thread_loop) return false;

  uint32_t sample_rate_value = get_sample_rate_value(sample_rate);

  pw_thread_loop_lock(thread_loop);

  uint32_t output_device_idx = get_output_device_index(current_output_device_id);
  char* output_node_name = output_devices[output_device_idx].node_name;

  if (!output.open(this, core, thread_loop, output_node_name, false, output_format,
                   sample_rate_value, num_output_channels, buffer_size)) {
    pw_thread_loop_unlock(thread_loop);
    return false;
  }

  if (current_input_device_id != WB_INVALID_AUDIO_DEVICE_INDEX) {
    uint32_t input_device_idx = get_input_device_index(current_input_device_id);
    char* input_node_name = input_devices[input_device_idx].node_name;

    if (!input.open(this, core, thread_loop, input_node_name, true, input_format,
                    sample_rate_value, num_input_channels, buffer_size)){
      output.close();
      pw_thread_loop_unlock(thread_loop);
      return false;
    }

    current_input_format = {
      .sample_rate = sample_rate,
      .sample_format = input_format,
      .num_channels = num_input_channels,
    };
  }

  current_output_format = {
    .sample_rate = sample_rate,
    .sample_format = output_format,
    .num_channels = num_output_channels,
  };

  stream_sample_rate = (double)sample_rate_value;
  stream_buffer_size = buffer_size;
  stream_period = buffer_size_to_period(buffer_size, sample_rate_value);
  stream_fn = stream_callback_fn;

  input_buffer.resize(buffer_size, true);
  input_buffer.resize_channel(num_input_channels);
  output_buffer.resize(buffer_size, true);
  output_buffer.resize_channel(num_output_channels);

  if (!input.start() || !output.start()) {
    input.close();
    output.close();
    pw_thread_loop_unlock(thread_loop);
    return false;
  }

  running = true;

  pw_thread_loop_unlock(thread_loop);

  return true;
}

void AudioIOPipeWire2::on_process_input(void* userdata) {
  ActiveDevicePipeWire2* device = static_cast<ActiveDevicePipeWire2*>(userdata);
  AudioIOPipeWire2* io = device->io;

  pw_buffer* buf = pw_stream_dequeue_buffer(device->stream);
  if (!buf)
    return;

  spa_buffer* spa_buf = buf->buffer;
  float* src = static_cast<float*>(spa_buf->datas[0].data);
  if (!src) {
    pw_stream_queue_buffer(device->stream, buf);
    return;
  }

  uint32_t n_frames = spa_buf->datas[0].chunk->size / (device->num_channels * sizeof(float));
  uint32_t num_channels = device->num_channels;

  for (uint32_t i = 0; i < n_frames && i < io->input_buffer.n_channels; i++) {
    for (uint32_t ch = 0; ch < num_channels; ch++) {
      io->input_buffer.channel_buffers[ch][i] = src[i * num_channels + ch];
    }
  }

  pw_stream_queue_buffer(device->stream, buf);
}

void AudioIOPipeWire2::on_process_output(void* userdata) {
  ActiveDevicePipeWire2* device = static_cast<ActiveDevicePipeWire2*>(userdata);
  AudioIOPipeWire2* io = device->io;

  pw_buffer* buf = pw_stream_dequeue_buffer(device->stream);
  if (!buf)
    return;

  spa_buffer* spa_buf = buf->buffer;
  float* dst = static_cast<float*>(spa_buf->datas[0].data);
  if (!dst) {
    pw_stream_queue_buffer(device->stream, buf);
    return;
  }

  io->stream_fn(io->output_buffer, io->input_buffer, io->stream_sample_rate);

  uint32_t n_frames = SPA_MIN(io->stream_buffer_size,
      spa_buf->datas[0].maxsize / (device->num_channels * sizeof(float)));
  uint32_t num_channels = device->num_channels;

  for (uint32_t i = 0; i < n_frames; i++) {
    for (uint32_t ch = 0; ch < num_channels; ch++) {
      dst[i * num_channels + ch] = io->output_buffer.channel_buffers[ch][i];
    }
  }

  spa_buf->datas[0].chunk->offset = 0;
  spa_buf->datas[0].chunk->stride = num_channels * sizeof(float);
  spa_buf->datas[0].chunk->size = n_frames * num_channels * sizeof(float);

  pw_stream_queue_buffer(device->stream, buf);
}

AudioIO2* create_audio_io_pipewire() {
  AudioIOPipeWire2* audio_io = new (std::nothrow) AudioIOPipeWire2();
  if (!audio_io)
    return nullptr;

  if (!audio_io->init()) {
    delete audio_io;
    return nullptr;
  }

  return static_cast<AudioIO2*>(audio_io);
}

}

#else

namespace wb {

AudioIO2* create_audio_io_pipewire() {
  return nullptr;
}

}

#endif