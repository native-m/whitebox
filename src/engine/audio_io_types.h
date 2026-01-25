#pragma once

#include "core/audio_format.h"

namespace wb {

using AudioDeviceID = uint64_t;
using AudioDevicePeriod = int64_t;

enum class AudioIOType {
  NoAudio = 0,
  WASAPI,
  ASIO,       // Unimplemented
  CoreAudio,  // Unimplemented
  PulseAudio, // Unimplemented
  PipeWire,
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
  AudioFormat sample_format;
  uint32_t num_channels;
};

}  // namespace wb