#include "audio_io.h"

#ifdef WB_PLATFORM_MACOS

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <optional>

#include "core/core_math.h"
#include "core/debug.h"
#include "core/defer.h"
#include "extern/xxhash.h"

namespace wb {

struct AudioDeviceCoreAudio {
  ::AudioDeviceID id;
  uint32_t index;
  uint32_t num_channels;
  AudioDeviceProperties properties;
};

struct AudioDeviceStreamFormat {
  AudioStreamID stream_id;
  AudioDeviceSampleRate sample_rate;
};

struct ActiveAudioDeviceCoreAudio {
  ::AudioDeviceID device_id;
  uint32_t min_buffer_size;
  uint32_t max_buffer_size;
  uint32_t buffer_size;
  uint32_t latency;
  uint32_t supported_sample_rate_flags;
  uint32_t max_sample_rate;
  AudioDeviceSampleRate shared_mode_sample_rate;

  bool open(::AudioDeviceID device_id, AudioDeviceType type);
  void close();
};

struct AudioIOCoreAudio final : public AudioIO2 {
  Vector<AudioDeviceCoreAudio> input_devices;
  Vector<AudioDeviceCoreAudio> output_devices;
  ActiveAudioDeviceCoreAudio input;
  ActiveAudioDeviceCoreAudio output;
  uint32_t min_buffer_size;
  uint32_t max_buffer_size;
  uint32_t buffer_size;

  bool init();

  bool rescan_device() override;

  uint32_t get_input_device_index(AudioDeviceID id) const override;

  uint32_t get_output_device_index(AudioDeviceID id) const override;

  const AudioDeviceProperties& get_input_device_properties(uint32_t device_idx) const override;

  const AudioDeviceProperties& get_output_device_properties(uint32_t device_idx) const override;

  bool is_stream_running() const override {
    return false;
  }

  bool is_on_the_same_driver(AudioDeviceType a_type, uint32_t a_device, AudioDeviceType b_type, uint32_t b_device)
      const override {
    return false;
  }

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
      AudioStreamFn stream_callback_fn) override {
    return false;
  }
};

//

static inline AudioDeviceID hash_device_uid(CFStringRef device_uid) {
  const CFIndex len = ::CFStringGetLength(device_uid);  // 16-bit unicode character
  UniChar* str = (UniChar*)std::malloc(len * 2);
  ::CFStringGetCharacters(device_uid, CFRangeMake(0, len), str);
  AudioDeviceID hash_id = XXH3_64bits(str, len * 2);
  std::free(str);
  return hash_id;
}

template<typename T>
static inline std::optional<T>
aobj_get_property(::AudioObjectID id, ::AudioObjectPropertySelector selector, ::AudioObjectPropertyScope scope) {
  ::AudioObjectPropertyAddress addr{
    .mSelector = selector,
    .mScope = scope,
  };

  T ret;
  uint32_t data_size = sizeof(T);
  OSStatus s = ::AudioObjectGetPropertyData(id, &addr, 0, nullptr, &data_size, &ret);
  if (s != noErr) {
    return {};
  }

  return ret;
}

template<typename T>
static inline Vector<T>
aobj_get_array_property(::AudioObjectID id, ::AudioObjectPropertySelector selector, ::AudioObjectPropertyScope scope) {
  ::AudioObjectPropertyAddress addr{
    .mSelector = selector,
    .mScope = scope,
  };

  uint32_t data_size;
  OSStatus s = ::AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &data_size);
  if (s != noErr) {
    return {};
  }

  Vector<T> ret(data_size / sizeof(T));
  s = ::AudioObjectGetPropertyData(id, &addr, 0, nullptr, &data_size, ret.data());
  if (s != noErr) {
    return {};
  }

  return ret;
}

template<typename T>
static inline bool aobj_get_array_property_inplace(
    ::AudioObjectID id,
    ::AudioObjectPropertySelector selector,
    ::AudioObjectPropertyScope scope,
    Vector<T>& out) {
  ::AudioObjectPropertyAddress addr{
    .mSelector = selector,
    .mScope = scope,
  };

  uint32_t data_size;
  OSStatus s = ::AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &data_size);
  if (s != noErr) {
    return false;
  }

  out.resize(data_size / sizeof(T));
  s = ::AudioObjectGetPropertyData(id, &addr, 0, nullptr, &data_size, out.data());
  if (s != noErr) {
    return false;
  }

  return true;
}

//

bool ActiveAudioDeviceCoreAudio::open(::AudioDeviceID device_id, AudioDeviceType type) {
  ::AudioObjectPropertyScope scope =
      type == AudioDeviceType::Input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;

  auto latency_value = aobj_get_property<uint32_t>(device_id, kAudioDevicePropertyLatency, scope);
  auto safety_offset = aobj_get_property<uint32_t>(device_id, kAudioDevicePropertySafetyOffset, scope);

  auto device_buffer_size = aobj_get_property<uint32_t>(device_id, kAudioDevicePropertyBufferFrameSize, scope);
  if (!device_buffer_size)
    return false;

  auto buffer_size_range = aobj_get_property<::AudioValueRange>(device_id, kAudioDevicePropertyBufferFrameSizeRange, scope);
  if (!buffer_size_range)
    return false;

  auto current_sample_rate = aobj_get_property<double>(device_id, kAudioDevicePropertyNominalSampleRate, scope);
  if (!current_sample_rate)
    return false;

  Vector<::AudioValueRange> supported_sample_rates =
      aobj_get_array_property<::AudioValueRange>(device_id, kAudioDevicePropertyAvailableNominalSampleRates, scope);

  // Check for supported sample rates
  uint32_t supported_sr = 0;
  uint32_t maximum_supported_sr = 0;
  for (const auto& sr_range : supported_sample_rates) {
    uint32_t min_sr = sr_range.mMinimum;
    uint32_t max_sr = sr_range.mMaximum;

    for (const auto [sr, device_sr] : compatible_sample_rates) {
      if (math::in_range(sr, min_sr, max_sr)) {
        supported_sr |= 1u << (uint32_t)device_sr;
        maximum_supported_sr = std::max(maximum_supported_sr, sr);
      }
    }
  }

  if (supported_sr == 0)
    return false;

  min_buffer_size = (uint32_t)buffer_size_range->mMinimum;
  max_buffer_size = (uint32_t)buffer_size_range->mMaximum;
  buffer_size = *device_buffer_size;
  latency = *latency_value + *safety_offset;
  supported_sample_rate_flags = supported_sr;
  max_sample_rate = maximum_supported_sr;
  shared_mode_sample_rate = get_device_sample_rate(*current_sample_rate);

  return true;
}

void ActiveAudioDeviceCoreAudio::close() {
  min_buffer_size = 0;
  max_buffer_size = 0;
  buffer_size = 0;
  latency = 0;
  supported_sample_rate_flags = 0;
  max_sample_rate = 0;
  shared_mode_sample_rate = {};
}

//

bool AudioIOCoreAudio::init() {
  exclusive_mode_support = false;
  shared_mode_support = true;
  return rescan_device();
}

bool AudioIOCoreAudio::rescan_device() {
  ::AudioObjectPropertyAddress property_addr{
    .mSelector = kAudioHardwarePropertyDevices,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain,
  };

  uint32_t data_size;

  Vector<::AudioDeviceID> audio_devices = aobj_get_array_property<::AudioDeviceID>(
      kAudioObjectSystemObject, kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal);

  auto default_input_device_id = aobj_get_property<::AudioDeviceID>(
      kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal);

  auto default_output_device_id = aobj_get_property<::AudioDeviceID>(
      kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal);

  uint32_t input_index = 0;
  uint32_t output_index = 0;

  for (auto id : audio_devices) {
    OSStatus s;

    auto name = aobj_get_property<CFStringRef>(id, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal);
    if (!name)
      continue;
    defer(::CFRelease(*name));

    auto device_uid = aobj_get_property<CFStringRef>(id, kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal);
    if (!device_uid)
      continue;
    defer(::CFRelease(*device_uid));

    // Check if this device is an input device
    property_addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    property_addr.mScope = kAudioObjectPropertyScopeInput;
    s = ::AudioObjectGetPropertyDataSize(id, &property_addr, 0, nullptr, &data_size);
    if (s == noErr && data_size > 8) {
      ::AudioBufferList* buffer_list = (::AudioBufferList*)std::malloc(data_size);
      ::AudioObjectGetPropertyData(id, &property_addr, 0, nullptr, &data_size, buffer_list);

      // Get number of channels
      uint32_t num_channels = 0;
      for (uint32_t i = 0; i < buffer_list->mNumberBuffers; i++) {
        num_channels += buffer_list->mBuffers[i].mNumberChannels;
      }

      AudioDeviceCoreAudio& input = input_devices.emplace_back();
      input.id = id;
      input.index = input_index;
      input.num_channels = num_channels;
      input.properties.id = hash_device_uid(*device_uid);
      input.properties.io_type = AudioIOType::CoreAudio;
      input.properties.type = AudioDeviceType::Input;
      ::CFStringGetCString(*name, input.properties.name, sizeof(input.properties.name), kCFStringEncodingUTF8);

      if (id == default_input_device_id) {
        default_input_device = input.properties;
      }

      std::free(buffer_list);
      input_index++;
    }

    // Check if this device is an output device
    property_addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    property_addr.mScope = kAudioObjectPropertyScopeOutput;
    s = ::AudioObjectGetPropertyDataSize(id, &property_addr, 0, nullptr, &data_size);
    if (s == noErr && data_size > 8) {
      ::AudioBufferList* buffer_list = (::AudioBufferList*)std::malloc(data_size);
      ::AudioObjectGetPropertyData(id, &property_addr, 0, nullptr, &data_size, buffer_list);

      // Get number of channels
      uint32_t num_channels = 0;
      for (uint32_t i = 0; i < buffer_list->mNumberBuffers; i++) {
        num_channels += buffer_list->mBuffers[i].mNumberChannels;
      }

      AudioDeviceCoreAudio& output = output_devices.emplace_back();
      output.id = id;
      output.index = output_index;
      output.num_channels = num_channels;
      output.properties.id = hash_device_uid(*device_uid);
      output.properties.io_type = AudioIOType::CoreAudio;
      output.properties.type = AudioDeviceType::Output;
      ::CFStringGetCString(*name, output.properties.name, sizeof(output.properties.name), kCFStringEncodingUTF8);

      if (id == default_output_device_id) {
        default_output_device = output.properties;
      }

      std::free(buffer_list);
      output_index++;
    }
  }

  return true;
}

uint32_t AudioIOCoreAudio::get_input_device_index(AudioDeviceID id) const {
  for (const auto& device : input_devices) {
    if (id == device.properties.id) {
      return device.index;
    }
  }
  return WB_INVALID_AUDIO_DEVICE_INDEX;
}

uint32_t AudioIOCoreAudio::get_output_device_index(AudioDeviceID id) const {
  for (const auto& device : output_devices) {
    if (id == device.properties.id) {
      return device.index;
    }
  }
  return WB_INVALID_AUDIO_DEVICE_INDEX;
}

const AudioDeviceProperties& AudioIOCoreAudio::get_input_device_properties(uint32_t device_idx) const {
  return input_devices[device_idx].properties;
}

const AudioDeviceProperties& AudioIOCoreAudio::get_output_device_properties(uint32_t device_idx) const {
  return output_devices[device_idx].properties;
}

bool AudioIOCoreAudio::open_device(uint32_t input_device_idx, uint32_t output_device_idx) {
  // Output device must be specified
  if (output_device_idx == WB_INVALID_AUDIO_DEVICE_INDEX)
    return false;

  AudioDeviceID input_device_id = 0;
  if (input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX) {
    if (input_device_idx < input_devices.size()) {
      AudioDeviceCoreAudio& input_device = input_devices[input_device_idx];
      if (!input.open(input_device.id, AudioDeviceType::Input)) {
        return false;
      }
    }
  }

  AudioDeviceID output_device_id = 0;
  if (output_device_idx < output_devices.size()) {
    AudioDeviceCoreAudio& output_device = output_devices[output_device_idx];
    if (!output.open(output_device.id, AudioDeviceType::Output)) {
      return false;
    }
  }

  static constexpr uint32_t min_supported_buffer_size = 32;
  uint32_t absolute_max_sample_rate = math::min(input.max_sample_rate, output.max_sample_rate);
  uint32_t absolute_min_buffer_size =
      math::max(math::max(input.min_buffer_size, output.min_buffer_size), min_supported_buffer_size);

  current_input_device_id = input_device_id;
  current_output_device_id = output_device_id;
  min_period = buffer_size_to_period(absolute_min_buffer_size, absolute_max_sample_rate);
  buffer_alignment = 32;

  shared_mode_input_format = AudioFormat::F32;
  shared_mode_output_format = AudioFormat::F32;
  shared_mode_sample_rate = output.shared_mode_sample_rate;
  exclusive_input_sample_rate_bit_flags = input.supported_sample_rate_flags;
  exclusive_output_sample_rate_bit_flags = output.supported_sample_rate_flags;
  exclusive_sample_rate_bit_flags = exclusive_input_sample_rate_bit_flags & exclusive_output_sample_rate_bit_flags;
  open = true;

  return true;
}

void AudioIOCoreAudio::close_device() {
  input.close();
  output.close();
  open = false;
  min_period = 0;
  buffer_alignment = 0;
  max_input_channel_count = 0;
  max_output_channel_count = 0;
  current_input_format = {};
  current_output_format = {};
}

AudioIO2* create_audio_io_coreaudio() {
  AudioIOCoreAudio* audio_io = new (std::nothrow) AudioIOCoreAudio();
  if (!audio_io)
    return nullptr;
  if (!audio_io->init())
    return nullptr;
  return audio_io;
}

}  // namespace wb

#else

namespace wb {

AudioIO2* create_audio_io_coreaudio() {
  return nullptr;
}

}  // namespace wb

#endif
