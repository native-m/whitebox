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

struct AudioDeviceBufferInfoCoreAudio {
  ::AudioStreamID stream_id;
  uint32_t bytes_per_channel;
  uint32_t buffer_length;
  uint32_t num_channels;
};

struct ActiveAudioDeviceCoreAudio {
  ::AudioDeviceID device_id;
  ::CFStringRef device_uid;
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

  bool stream_running = false;
  bool is_device_aggregated = false;
  ::AudioDeviceID device_id;
  ::AudioDeviceIOProcID proc_id;
  AudioStreamFn stream_fn;

  double current_sample_rate = 0.0;
  Vector<AudioDeviceBufferInfoCoreAudio> in_buffer_info;
  Vector<AudioDeviceBufferInfoCoreAudio> out_buffer_info;
  AudioBuffer<float> in_buffer;
  AudioBuffer<float> out_buffer;

  bool init();

  bool rescan_device() override;

  uint32_t get_input_device_index(AudioDeviceID id) const override;

  uint32_t get_output_device_index(AudioDeviceID id) const override;

  const AudioDeviceProperties& get_input_device_properties(uint32_t device_idx) const override;

  const AudioDeviceProperties& get_output_device_properties(uint32_t device_idx) const override;

  bool is_stream_running() const override {
    return stream_running;
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
      AudioStreamFn stream_callback_fn) override;

  static OSStatus audio_callback(
      ::AudioObjectID inDevice,
      const ::AudioTimeStamp* inNow,
      const ::AudioBufferList* inInputData,
      const ::AudioTimeStamp* inInputTime,
      ::AudioBufferList* outOutputData,
      const ::AudioTimeStamp* inOutputTime,
      void* __nullable inClientData);
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
static inline bool aobj_set_property(
    ::AudioObjectID id,
    ::AudioObjectPropertySelector selector,
    ::AudioObjectPropertyScope scope,
    const T& value) {
  ::AudioObjectPropertyAddress addr{
    .mSelector = selector,
    .mScope = scope,
  };

  OSStatus s = ::AudioObjectSetPropertyData(id, &addr, 0, nullptr, sizeof(T), &value);
  return s != noErr;
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
static inline HeapBlock<T>
aobj_get_property_block(::AudioObjectID id, ::AudioObjectPropertySelector selector, ::AudioObjectPropertyScope scope) {
  ::AudioObjectPropertyAddress addr{
    .mSelector = selector,
    .mScope = scope,
  };

  uint32_t data_size = 0;
  OSStatus s = ::AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &data_size);
  if (s != noErr || data_size == 0) {
    return {};
  }

  HeapBlock<T> ret(data_size);
  s = ::AudioObjectGetPropertyData(id, &addr, 0, nullptr, &data_size, ret.data());
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

static Vector<AudioDeviceBufferInfoCoreAudio> make_buffer_info(
    const Vector<::AudioStreamID>& stream_ids,
    const AudioBufferList* buffer_list) {
  Vector<AudioDeviceBufferInfoCoreAudio> buffer_info;
  ::AudioStreamBasicDescription current_stream;

  for (uint32_t buffer_idx = 0; auto id : stream_ids) {
    auto stream_desc = aobj_get_property<::AudioStreamBasicDescription>(
                           id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal)
                           .value();

    uint32_t bytes_per_channel = stream_desc.mBitsPerChannel / 8;
    const ::AudioBuffer& buffer = buffer_list->mBuffers[buffer_idx];
    if (buffer.mNumberChannels == 1) {
      // Non-interleaved case
      uint32_t next_buffer_idx = buffer_idx + stream_desc.mChannelsPerFrame;
      for (uint32_t i = buffer_idx; i < next_buffer_idx; i++) {
        const ::AudioBuffer& buffer = buffer_list->mBuffers[i];
        buffer_info.push_back(
            {
              .stream_id = id,
              .bytes_per_channel = bytes_per_channel,
              .buffer_length = buffer.mDataByteSize / stream_desc.mBytesPerFrame,
              .num_channels = buffer.mNumberChannels,
            });
      }
      buffer_idx = next_buffer_idx;
    } else {
      // Interleaved case
      buffer_info.push_back(
          {
            .stream_id = id,
            .bytes_per_channel = bytes_per_channel,
            .buffer_length = buffer.mDataByteSize / stream_desc.mBytesPerFrame,
            .num_channels = buffer.mNumberChannels,
          });
      buffer_idx++;
    }
  }

  return buffer_info;
}

//

bool ActiveAudioDeviceCoreAudio::open(::AudioDeviceID id, AudioDeviceType type) {
  ::AudioObjectPropertyScope scope =
      type == AudioDeviceType::Input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;

  auto device_uid_str = aobj_get_property<CFStringRef>(id, kAudioDevicePropertyDeviceUID, scope);
  if (!device_uid_str)
    return false;

  auto latency_value = aobj_get_property<uint32_t>(id, kAudioDevicePropertyLatency, scope);
  auto safety_offset = aobj_get_property<uint32_t>(id, kAudioDevicePropertySafetyOffset, scope);

  auto device_buffer_size = aobj_get_property<uint32_t>(id, kAudioDevicePropertyBufferFrameSize, scope);
  if (!device_buffer_size)
    return false;

  auto buffer_size_range = aobj_get_property<::AudioValueRange>(id, kAudioDevicePropertyBufferFrameSizeRange, scope);
  if (!buffer_size_range)
    return false;

  auto current_sample_rate = aobj_get_property<double>(id, kAudioDevicePropertyNominalSampleRate, scope);
  if (!current_sample_rate)
    return false;

  Vector<::AudioValueRange> supported_sample_rates =
      aobj_get_array_property<::AudioValueRange>(id, kAudioDevicePropertyAvailableNominalSampleRates, scope);

  // Check for supported sample rates
  uint32_t supported_sr = 0;
  uint32_t maximum_supported_sr = 0;
  for (const auto& sr_range : supported_sample_rates) {
    uint32_t min_sr = sr_range.mMinimum;
    uint32_t max_sr = sr_range.mMaximum;

    for (const auto [sr, device_sr] : compatible_sample_rates) {
      if (math::in_range_inclusive(sr, min_sr, max_sr)) {
        supported_sr |= 1u << (uint32_t)device_sr;
        maximum_supported_sr = std::max(maximum_supported_sr, sr);
      }
    }
  }

  if (supported_sr == 0)
    return false;

  device_id = id;
  device_uid = device_uid_str.value();
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
  device_id = 0;
  min_buffer_size = 0;
  max_buffer_size = 0;
  buffer_size = 0;
  latency = 0;
  supported_sample_rate_flags = 0;
  max_sample_rate = 0;
  shared_mode_sample_rate = {};
  ::CFRelease(device_uid);
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

  num_input_device = input_index;
  num_output_device = output_index;

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

  max_input_channel_count = input_devices[input_device_idx].num_channels;
  max_output_channel_count = output_devices[output_device_idx].num_channels;

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
  if (stream_running) {
    ::AudioDeviceStop(device_id, proc_id);
    ::AudioDeviceDestroyIOProcID(device_id, proc_id);

    if (is_device_aggregated)
      ::AudioHardwareDestroyAggregateDevice(device_id);
  }

  input.close();
  output.close();
  open = false;
  min_period = 0;
  buffer_alignment = 0;
  max_input_channel_count = 0;
  max_output_channel_count = 0;
  current_input_format = {};
  current_output_format = {};
  is_device_aggregated = false;
  stream_running = false;
}

bool AudioIOCoreAudio::start(
    bool exclusive_mode,
    uint32_t buffer_size,
    AudioDeviceSampleRate sample_rate,
    AudioFormat input_format,
    AudioFormat output_format,
    uint32_t num_input_channels,
    uint32_t num_output_channels,
    AudioThreadPriority priority,
    AudioStreamFn stream_callback_fn) {
  if (stream_running)
    return false;

  double sample_rate_value = get_sample_rate_value(sample_rate);
  ::AudioDeviceID main_device_id = output.device_id;
  bool is_aggregated = false;

  // Create an aggregate device if input and output device are different
  if (input.device_id != main_device_id) {
    int true_value = 1;
    int false_value = 0;
    int drift_compensation_quality_value = kAudioAggregateDriftCompensationMediumQuality;
    CFStringRef name = CFSTR("wb_aggregate");
    CFNumberRef true_num = ::CFNumberCreate(nullptr, kCFNumberIntType, &true_value);
    CFNumberRef false_num = ::CFNumberCreate(nullptr, kCFNumberIntType, &false_value);
    CFNumberRef drift_compensation_quality = ::CFNumberCreate(nullptr, kCFNumberIntType, &drift_compensation_quality_value);
    defer(::CFRelease(true_num));
    defer(::CFRelease(false_num));
    defer(::CFRelease(drift_compensation_quality));

    // Input sub-device of the aggregate device
    CFMutableDictionaryRef input_sub =
        ::CFDictionaryCreateMutable(nullptr, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    ::CFDictionaryAddValue(input_sub, CFSTR(kAudioSubDeviceUIDKey), input.device_uid);
    ::CFDictionaryAddValue(input_sub, CFSTR(kAudioSubDeviceDriftCompensationKey), true_num);
    ::CFDictionaryAddValue(input_sub, CFSTR(kAudioSubDeviceDriftCompensationQualityKey), drift_compensation_quality);
    defer(::CFRelease(input_sub));

    // Output sub-device of the aggregate device
    CFMutableDictionaryRef output_sub =
        ::CFDictionaryCreateMutable(nullptr, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    ::CFDictionaryAddValue(output_sub, CFSTR(kAudioSubDeviceUIDKey), output.device_uid);
    ::CFDictionaryAddValue(output_sub, CFSTR(kAudioSubDeviceDriftCompensationKey), true_num);
    ::CFDictionaryAddValue(output_sub, CFSTR(kAudioSubDeviceDriftCompensationQualityKey), drift_compensation_quality);
    defer(::CFRelease(output_sub));

    CFMutableArrayRef device_collection = ::CFArrayCreateMutable(nullptr, 2, &kCFTypeArrayCallBacks);
    ::CFArrayAppendValue(device_collection, input_sub);
    ::CFArrayAppendValue(device_collection, output_sub);
    defer(::CFRelease(device_collection));

    CFMutableDictionaryRef agg_desc =
        ::CFDictionaryCreateMutable(nullptr, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    ::CFDictionaryAddValue(agg_desc, CFSTR(kAudioAggregateDeviceNameKey), name);
    ::CFDictionaryAddValue(agg_desc, CFSTR(kAudioAggregateDeviceUIDKey), name);
    ::CFDictionaryAddValue(agg_desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), device_collection);
    ::CFDictionaryAddValue(agg_desc, CFSTR(kAudioAggregateDeviceMainSubDeviceKey), output.device_uid);
    ::CFDictionaryAddValue(agg_desc, CFSTR(kAudioAggregateDeviceIsPrivateKey), true_num);
    defer(::CFRelease(agg_desc));

    ::AudioDeviceID agg_id;
    OSStatus s = ::AudioHardwareCreateAggregateDevice(agg_desc, &agg_id);
    if (s != noErr) {
      return false;
    }

    main_device_id = agg_id;
    is_aggregated = true;
  }

  // Set sample rate
  aobj_set_property<double>(
      main_device_id, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, sample_rate_value);

  // Set buffer size
  aobj_set_property<uint32_t>(
      main_device_id, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, buffer_size);
  auto actual_buffer_size =
      aobj_get_property<uint32_t>(main_device_id, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);

  // Calculate roundtrip latency
  auto input_latency =
      aobj_get_property<uint32_t>(main_device_id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput).value();
  auto output_latency =
      aobj_get_property<uint32_t>(main_device_id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput).value();
  auto input_safety_offset =
      aobj_get_property<uint32_t>(main_device_id, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput).value();
  auto output_safety_offset =
      aobj_get_property<uint32_t>(main_device_id, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput).value();

  auto roundtrip_latency = input_latency + input_safety_offset + buffer_size + output_latency + output_safety_offset;

  Vector<::AudioStreamID> input_stream_ids =
      aobj_get_array_property<::AudioStreamID>(main_device_id, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeInput);
  Vector<::AudioStreamID> output_stream_ids =
      aobj_get_array_property<::AudioStreamID>(main_device_id, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput);
  auto input_stream_conf = aobj_get_property_block<::AudioBufferList>(
      main_device_id, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput);
  auto output_stream_conf = aobj_get_property_block<::AudioBufferList>(
      main_device_id, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput);

  ::AudioDeviceIOProcID io_proc;
  OSStatus s = ::AudioDeviceCreateIOProcID(main_device_id, audio_callback, this, &io_proc);
  if (s != noErr) {
    return false;
  }

  in_buffer_info = make_buffer_info(input_stream_ids, input_stream_conf.data());
  out_buffer_info = make_buffer_info(output_stream_ids, output_stream_conf.data());
  in_buffer.resize(buffer_size);
  in_buffer.resize_channel(num_input_channels);
  out_buffer.resize(buffer_size);
  out_buffer.resize_channel(num_output_channels);
  max_input_channel_count = num_input_channels;
  max_output_channel_count = num_output_channels;
  current_sample_rate = sample_rate_value;
  stream_fn = stream_callback_fn;

  ::AudioDeviceStart(main_device_id, io_proc);

  device_id = main_device_id;
  proc_id = io_proc;
  is_device_aggregated = is_aggregated;
  stream_running = true;
  return true;
}

OSStatus AudioIOCoreAudio::audio_callback(
    ::AudioObjectID inDevice,
    const ::AudioTimeStamp* inNow,
    const ::AudioBufferList* inInputData,
    const ::AudioTimeStamp* inInputTime,
    ::AudioBufferList* outOutputData,
    const ::AudioTimeStamp* inOutputTime,
    void* __nullable inClientData) {
  AudioIOCoreAudio* instance = (AudioIOCoreAudio*)inClientData;
  uint32_t input_channel = 0;
  uint32_t output_channel = 0;
  uint32_t input_buf_idx = 0;
  uint32_t output_buf_idx = 0;

  // De-interleave every interleaved channel buffer
  while (input_channel < instance->max_input_channel_count) {
    AudioDeviceBufferInfoCoreAudio& buffer_info = instance->in_buffer_info[input_buf_idx];
    const ::AudioBuffer& buffer = inInputData->mBuffers[input_buf_idx];
    uint32_t num_channels = buffer_info.num_channels;
    if (num_channels == 1) {
      float* dst = instance->in_buffer.get_write_pointer(input_channel);
      std::memcpy(dst, buffer.mData, buffer.mDataByteSize);
    } else {
      float* const* dst = &instance->in_buffer.channel_buffers[input_channel];
      convert_to_deinterleaved_f32(dst, (float*)buffer.mData, 0, buffer_info.buffer_length, num_channels);
    }
    input_channel += num_channels;
  }

  instance->stream_fn(instance->out_buffer, instance->in_buffer, instance->current_sample_rate);

  while (output_channel < instance->max_output_channel_count) {
    AudioDeviceBufferInfoCoreAudio& buffer_info = instance->out_buffer_info[input_buf_idx];
    ::AudioBuffer& buffer = outOutputData->mBuffers[output_buf_idx];
    uint32_t num_channels = buffer_info.num_channels;
    if (num_channels == 1) {
      const float* src = instance->out_buffer.get_read_pointer(output_channel);
      std::memcpy(buffer.mData, src, buffer.mDataByteSize);
    } else {
      const float* const* src = &instance->out_buffer.channel_buffers[output_channel];
      convert_to_interleaved_f32((float*)buffer.mData, src, 0, buffer_info.buffer_length, num_channels);
    }
    output_channel += num_channels;
  }

  return noErr;
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
