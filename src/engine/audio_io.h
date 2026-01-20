#pragma once

#include <functional>
// #include <string>

#include "audio_io_types.h"
#include "core/audio_buffer.h"
#include "core/bit_manipulation.h"
#include "core/common.h"
#include "core/core_math.h"
#include "core/vector.h"

#define WB_INVALID_AUDIO_DEVICE_INDEX (~0U)

namespace wb {

struct Engine;

using AudioDeviceRemovedFn = void (*)(void* userdata, bool rescan_only);
using AudioDeviceFormatChangedFn = void (*)(void* userdata);
using AudioStreamFn =
    void (*)(AudioBuffer<float>& output_buffer, const AudioBuffer<float>& input_buffer, double sample_rate);

struct AudioIO2 {
  uint32_t num_output_device{};
  uint32_t num_input_device{};
  AudioDeviceProperties default_input_device;
  AudioDeviceProperties default_output_device;
  AudioDeviceID current_input_device_id{};
  AudioDeviceID current_output_device_id{};

  bool shared_mode_support{};
  bool exclusive_mode_support{};
  AudioDevicePeriod min_period{};
  uint32_t buffer_alignment{};
  int32_t max_input_channel_count{};
  int32_t max_output_channel_count{};
  uint32_t exclusive_sample_rate_bit_flags{};
  uint32_t exclusive_input_format_bit_flags{};
  uint32_t exclusive_output_format_bit_flags{};
  uint32_t exclusive_input_sample_rate_bit_flags{};
  uint32_t exclusive_output_sample_rate_bit_flags{};
  AudioFormat shared_mode_input_format{};
  AudioFormat shared_mode_output_format{};
  AudioDeviceSampleRate shared_mode_sample_rate{};
  AudioDeviceFormat current_input_format{};
  AudioDeviceFormat current_output_format{};
  Pair<AudioDeviceRemovedFn, void*> device_removed_listener_fn{};
  Pair<AudioDeviceFormatChangedFn, void*> device_format_changed_listener_fn{};
  volatile bool open{};

  virtual ~AudioIO2() {
  }

  bool is_device_open() const {
    return open;
  }

  /*
      Check if sample rate is supported. This function only valid if the audio device has been opened.
  */
  bool is_sample_rate_supported(AudioDeviceSampleRate sample_rate) const {
    return has_bit_enum(exclusive_sample_rate_bit_flags, sample_rate);
  }

  /*
      Check if sample rate is supported by device type. This function only valid if the audio device has been opened.
  */
  bool is_sample_rate_supported(AudioDeviceType device_type, AudioDeviceSampleRate sample_rate) const {
    uint32_t flags = (device_type == AudioDeviceType::Input) ? exclusive_input_sample_rate_bit_flags
                                                             : exclusive_output_sample_rate_bit_flags;
    return has_bit_enum(flags, sample_rate);
  }

  /*
      Check if input sample format is supported. This function only valid if the audio device has been opened.
  */
  bool is_input_sample_format_supported(AudioFormat format) const {
    return has_bit_enum(exclusive_input_format_bit_flags, format);
  }

  /*
      Check if output sample format is supported. This function only valid if the audio device has been opened.
  */
  bool is_output_sample_format_supported(AudioFormat format) const {
    return has_bit_enum(exclusive_output_format_bit_flags, format);
  }

  /*
       Get current stream input format. This function only valid if the audio stream has been started.
  */
  AudioDeviceFormat get_current_input_format() const {
    return current_input_format;
  }

  /*
       Get current stream output format. This function only valid if the audio stream has been started.
  */
  AudioDeviceFormat get_current_output_format() const {
    return current_input_format;
  }

  uint32_t get_max_input_channel_count() const {
    return max_input_channel_count;
  }

  uint32_t get_max_output_channel_count() const {
    return max_output_channel_count;
  }

  uint32_t get_input_device_count() const {
    return num_input_device;
  }

  uint32_t get_output_device_count() const {
    return num_output_device;
  }

  virtual bool rescan_device() = 0;
  virtual uint32_t get_input_device_index(AudioDeviceID id) const = 0;
  virtual uint32_t get_output_device_index(AudioDeviceID id) const = 0;
  virtual const AudioDeviceProperties& get_input_device_properties(uint32_t device_idx) const = 0;
  virtual const AudioDeviceProperties& get_output_device_properties(uint32_t device_idx) const = 0;
  virtual bool is_stream_running() const = 0;
  virtual bool is_on_the_same_driver(AudioDeviceType a_type, uint32_t a_device, AudioDeviceType b_type, uint32_t b_device)
      const = 0;

  virtual bool open_device(uint32_t input_device_idx, uint32_t output_device_idx) = 0;
  virtual void close_device() = 0;
  virtual bool start(
      bool exclusive_mode,
      uint32_t buffer_size,
      AudioDeviceSampleRate sample_rate,
      AudioFormat input_format,
      AudioFormat output_format,
      uint32_t num_input_channels,
      uint32_t num_output_channels,
      AudioThreadPriority priority,
      AudioStreamFn stream_callback_fn) = 0;

  void set_device_removed_listener(void* userdata, AudioDeviceRemovedFn fn) {
    device_removed_listener_fn = { fn, userdata };
  }

  void set_device_format_changed_listener(void* userdata, AudioDeviceFormatChangedFn fn) {
    device_format_changed_listener_fn = { fn, userdata };
  }

  // Only call this in the implementation class!
  void reset_state();

  static AudioIO2* create(AudioIOType type);
  static AudioIOType get_platform_recommended_audio_io_type();
};

inline static uint32_t period_to_buffer_size(AudioDevicePeriod period, uint32_t sample_rate) {
  constexpr double unit_100_ns = 10000000.0;
  return (uint32_t)math::round(sample_rate * period / unit_100_ns);
}

inline static double period_to_ms(AudioDevicePeriod period) {
  constexpr double unit_100_ns = 10000000.0;
  return 1000.0 * period / unit_100_ns;
}

inline static AudioDevicePeriod buffer_size_to_period(uint32_t buffer_size, uint32_t sample_rate) {
  constexpr double unit_100_ns = 10000000.0;
  return (AudioDevicePeriod)math::round(unit_100_ns * (buffer_size / (double)sample_rate));
}

inline static uint32_t get_sample_rate_value(AudioDeviceSampleRate sr_enum) {
  switch (sr_enum) {
    case AudioDeviceSampleRate::Hz44100: return 44100;
    case AudioDeviceSampleRate::Hz48000: return 48000;
    case AudioDeviceSampleRate::Hz88200: return 88200;
    case AudioDeviceSampleRate::Hz96000: return 96000;
    case AudioDeviceSampleRate::Hz176400: return 176400;
    case AudioDeviceSampleRate::Hz192000: return 192000;
    default: break;
  }
  return 0;
}

inline static AudioDeviceSampleRate get_device_sample_rate(uint32_t sample_rate) {
  switch (sample_rate) {
    case 44100: return AudioDeviceSampleRate::Hz44100;
    case 48000: return AudioDeviceSampleRate::Hz48000;
    case 88200: return AudioDeviceSampleRate::Hz88200;
    case 96000: return AudioDeviceSampleRate::Hz96000;
    case 176400: return AudioDeviceSampleRate::Hz176400;
    case 192000: return AudioDeviceSampleRate::Hz192000;
    default: break;
  }
  return {};
}

static const std::pair<uint32_t, AudioDeviceSampleRate> compatible_sample_rates[] = {
  { 44100, AudioDeviceSampleRate::Hz44100 },   { 48000, AudioDeviceSampleRate::Hz48000 },
  { 88200, AudioDeviceSampleRate::Hz88200 },   { 96000, AudioDeviceSampleRate::Hz96000 },
  { 176400, AudioDeviceSampleRate::Hz176400 }, { 192000, AudioDeviceSampleRate::Hz192000 },
};

static const AudioFormat compatible_formats[] = {
  AudioFormat::I16, AudioFormat::I24, AudioFormat::I24_X8, AudioFormat::I32, AudioFormat::F32,
};

static const uint16_t compatible_channel_count[] = {
  1,
  2,
};

}  // namespace wb