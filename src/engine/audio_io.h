#pragma once

#include <functional>
#include <string>

#include "core/audio_format.h"
#include "core/bit_manipulation.h"
#include "core/common.h"
#include "core/core_math.h"

#define WB_INVALID_AUDIO_DEVICE_INDEX (~0U)

namespace wb {

struct Engine;

using AudioDeviceID = uint64_t;
using AudioDevicePeriod = int64_t;
using AudioDeviceRemovedCb = void (*)(void* userdata);

enum class AudioIOType {
  WASAPI,
  ASIO,       // Unimplemented
  CoreAudio,  // Unimplemented
  PulseAudio,
};

enum class AudioDeviceType {
  Input,
  Output,
};

enum class AudioDeviceSampleRate {
  Hz44100,
  Hz48000,
  Hz88200,
  Hz96000,
  Hz176400,
  Hz192000,
  Max,
};

enum class AudioThreadPriority {
  Lowest,
  Low,
  Normal,
  High,
  Highest,
};

struct AudioDeviceProperties {
  char name[128]{};
  AudioDeviceID id;
  AudioDeviceType type;
  AudioIOType io_type;
};

struct AudioDeviceFormat {
  AudioDeviceSampleRate sample_rate;
  AudioFormat output_sample_format;
  uint16_t output_channels;
  uint16_t input_channels;
};

struct AudioIO {
  static constexpr int max_channel_map = 64;

  AudioDeviceRemovedCb device_removed_cb{};
  AudioDeviceProperties default_input_device;
  AudioDeviceProperties default_output_device;
  AudioDeviceID current_input_device_id{ 0 };
  AudioDeviceID current_output_device_id{ 0 };
  uint32_t input_device_count = 0;
  uint32_t output_device_count = 0;
  int32_t max_input_channel_count = 0;
  int32_t max_output_channel_count = 0;
  uint32_t exclusive_sample_rate_bit_flags = 0;
  uint32_t exclusive_input_format_bit_flags = 0;
  uint32_t exclusive_output_format_bit_flags = 0;
  AudioFormat shared_mode_output_format{};
  AudioFormat shared_mode_input_format{};
  AudioDeviceSampleRate shared_mode_sample_rate{};
  AudioDevicePeriod min_period = 0;
  bool low_latency_shared_mode{};
  uint32_t buffer_alignment = 0;
  bool open = false;

  uint32_t get_input_device_count() const {
    return input_device_count;
  }
  uint32_t get_output_device_count() const {
    return output_device_count;
  }
  bool is_open() const {
    return open;
  }

  /*
      Check if sample rate is supported. This function only valid if the audio device has been opened
  */
  bool is_sample_rate_supported(AudioDeviceSampleRate sample_rate) const {
    return has_bit_enum(exclusive_sample_rate_bit_flags, sample_rate);
  }

  /*
      Check if input format is supported. This function only valid if the audio device has been opened
  */
  bool is_input_format_supported(AudioFormat format) const {
    return has_bit_enum(exclusive_input_format_bit_flags, format);
  }

  /*
      Check if output format is supported. This function only valid if the audio device has been opened
  */
  bool is_output_format_supported(AudioFormat format) const {
    return has_bit_enum(exclusive_output_format_bit_flags, format);
  }

  int32_t get_max_input_channels() const {
    return max_input_channel_count;
  }
  int32_t get_max_output_channels() const {
    return max_output_channel_count;
  }


  void set_on_device_removed_cb(AudioDeviceRemovedCb cb) {
    device_removed_cb = cb;
  }

  virtual ~AudioIO() {
  }

  /*
    Check for exclusive mode support
  */
  virtual bool exclusive_mode_support() {
    return false;
  }

  /*
    Check if the audio device can be shared
  */
  virtual bool shared_mode_support() {
    return false;
  }

  /*
      Rescan available device that can be used by whitebox.
  */
  virtual bool rescan_devices() = 0;

  virtual uint32_t get_input_device_index(AudioDeviceID id) const = 0;
  virtual uint32_t get_output_device_index(AudioDeviceID id) const = 0;
  virtual const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const = 0;
  virtual const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const = 0;

  /*
      Open input and output devices to ensure they are ready for use.
      Usually, the implementation gets the hardware information in here.
  */
  virtual bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) = 0;

  /*
      Closes input and output devices after being used by the application.
  */
  virtual void close_device() = 0;

  /*
      Starts the audio engine.
  */
  virtual bool start(
      Engine* engine,
      bool exclusive_mode,
      uint32_t buffer_size,
      AudioFormat input_format,
      AudioFormat output_format,
      AudioDeviceSampleRate sample_rate,
      AudioThreadPriority priority) = 0;
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

void init_audio_io(AudioIOType type);
void shutdown_audio_io();

extern AudioIO* g_audio_io;

}  // namespace wb