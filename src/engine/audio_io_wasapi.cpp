#include "audio_io.h"

#ifdef WB_PLATFORM_WINDOWS
#include <Audioclient.h>
#include <audiopolicy.h>
#include <fmt/xchar.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl.h>

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string_view>

#include "core/debug.h"
#include "core/defer.h"
#include "core/vector.h"
#include "extern/xxhash.h"

#ifndef __mmdeviceapi_h__
#include <initguid.h>
#include <mmdeviceapi.h>
#endif

#include <Functiondiscoverykeys_devpkey.h>
#include <avrt.h>

using namespace Microsoft::WRL;

namespace wb {

struct AudioIOWASAPI2;

struct AudioDeviceWASAPI2 {
  CLSID container_id;
  uint32_t index;
  AudioDeviceProperties properties;
  wchar_t* impl_uid;

  ~AudioDeviceWASAPI2();
};

struct ActiveDeviceWASAPI2 {
  IMMDevice* device{};
  IAudioClient3* client{};
  HANDLE event{};

  UINT default_low_latency_buffer_size;
  UINT min_low_latency_buffer_size;
  UINT max_low_latency_buffer_size;
  UINT low_latency_buffer_alignment;

  AudioDevicePeriod default_low_latency_period;
  AudioDevicePeriod min_low_latency_period;
  AudioDevicePeriod max_low_latency_period;
  AudioDevicePeriod absolute_min_period;
  REFERENCE_TIME default_device_period;
  REFERENCE_TIME min_device_period;
  WAVEFORMATEXTENSIBLE shared_format{};

  bool open(IMMDevice* new_device);
  void close();
  bool init_stream(
      bool exclusive_mode,
      AudioDevicePeriod period,
      AudioFormat sample_format,
      AudioDeviceSampleRate sample_rate,
      uint32_t num_stream);
  void stop_stream();
};

struct SessionEventWASAPI : public IAudioSessionEvents {
  AudioIOWASAPI2* io;
  SessionEventWASAPI(AudioIOWASAPI2* audio_io);
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void __RPC_FAR* __RPC_FAR* ppvObject) override;
  ULONG STDMETHODCALLTYPE AddRef(void) override;
  ULONG STDMETHODCALLTYPE Release(void) override;
  HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) override;
  HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) override;
  HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) override;
  HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(
      DWORD ChannelCount,
      float NewChannelVolumeArray[],
      DWORD ChangedChannel,
      LPCGUID EventContext) override;
  HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) override;
  HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState NewState) override;
  HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) override;
};

struct DeviceNotificationWASAPI : public IMMNotificationClient {
  AudioIOWASAPI2* io;
  DeviceNotificationWASAPI(AudioIOWASAPI2* audio_io);
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void __RPC_FAR* __RPC_FAR* ppvObject) override;
  ULONG STDMETHODCALLTYPE AddRef(void) override;
  ULONG STDMETHODCALLTYPE Release(void) override;
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
};

struct AudioIOWASAPI2 final : public AudioIO2 {
  Vector<AudioDeviceWASAPI2> input_devices;
  Vector<AudioDeviceWASAPI2> output_devices;
  SessionEventWASAPI session_handler{ this };
  DeviceNotificationWASAPI device_notification_handler{ this };
  std::optional<AudioDeviceID> active_input_device_id;
  std::optional<AudioDeviceID> active_output_device_id;
  IAudioSessionEvents* session_event_handler{};
  IMMDeviceEnumerator* device_enumerator;
  IAudioRenderClient* render_client{};
  IAudioCaptureClient* capture_client{};
  ActiveDeviceWASAPI2 input;
  ActiveDeviceWASAPI2 output;
  AudioDevicePeriod stream_period{};
  uint32_t input_channel_mask{};
  uint32_t output_channel_mask{};
  uint32_t maximum_input_buffer_size{};
  uint32_t maximum_output_buffer_size{};
  uint32_t stream_buffer_size;
  double stream_sample_rate{};
  std::atomic_bool running;
  std::thread audio_thread;
  AudioStreamFn stream_fn{};

  ~AudioIOWASAPI2();
  bool init();
  bool scan_audio_endpoints(EDataFlow type, Vector<AudioDeviceWASAPI2>& device_list);
  uint32_t find_device_index(AudioDeviceType type, AudioDeviceID id) const;

  bool rescan_device() override;
  uint32_t get_input_device_index(AudioDeviceID id) const override;
  uint32_t get_output_device_index(AudioDeviceID id) const override;
  const AudioDeviceProperties& get_input_device_properties(uint32_t device_idx) const override;
  const AudioDeviceProperties& get_output_device_properties(uint32_t device_idx) const override;
  bool is_stream_running() const override;
  bool is_on_the_same_driver(AudioDeviceType a_type, uint32_t a_device, AudioDeviceType b_type, uint32_t b_device)
      const override;

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

  static void audio_thread_runner(AudioIOWASAPI2* io, AudioThreadPriority priority);
};

inline static AudioDeviceID make_device_id_hash(const std::wstring_view& str_id) {
  return XXH3_64bits(str_id.data(), str_id.size());
}

struct FormatBitSizes {
  GUID subtype;
  WORD bits_per_sample;
  WORD valid_bits_per_sample;
};

inline static FormatBitSizes get_bit_sizes(AudioFormat audio_format) {
  switch (audio_format) {
    case AudioFormat::I8: return { KSDATAFORMAT_SUBTYPE_PCM, 8, 8 };
    case AudioFormat::I16: return { KSDATAFORMAT_SUBTYPE_PCM, 16, 16 };
    case AudioFormat::I24: return { KSDATAFORMAT_SUBTYPE_PCM, 24, 24 };
    case AudioFormat::I24_X8: return { KSDATAFORMAT_SUBTYPE_PCM, 32, 24 };
    case AudioFormat::I32: return { KSDATAFORMAT_SUBTYPE_PCM, 32, 32 };
    case AudioFormat::F32: return { KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 32, 32 };
    default: break;
  }
  return {};
}

inline static WAVEFORMATEXTENSIBLE
make_waveformat(AudioFormat sample_format, uint32_t sample_rate, uint16_t channels, uint32_t channel_mask) {
  // NOTE: Some drivers does not work with WAVEFORMATEXTENSIBLE!
  auto [format, bits, valid_bits] = get_bit_sizes(sample_format);
  WAVEFORMATEXTENSIBLE waveformat{};

  if (bits <= 24) {
    waveformat.Format.wFormatTag = WAVE_FORMAT_PCM;
  } else {
    waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    waveformat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  }

  waveformat.Format.nChannels = channels;
  waveformat.Format.nSamplesPerSec = sample_rate;
  waveformat.Format.nBlockAlign = channels * bits / 8;
  waveformat.Format.nAvgBytesPerSec = waveformat.Format.nBlockAlign * sample_rate;
  waveformat.Format.wBitsPerSample = bits;
  waveformat.Samples.wValidBitsPerSample = valid_bits;
  waveformat.SubFormat = format;
  waveformat.dwChannelMask = channel_mask;

  return waveformat;
}

inline static std::optional<AudioDeviceFormat> convert_waveformat_to_device_format(const WAVEFORMATEXTENSIBLE& fmt) {
  AudioDeviceFormat ret;

  if (fmt.Format.wFormatTag == WAVE_FORMAT_PCM) {
    if (fmt.Format.wBitsPerSample == 16) {
      ret.sample_format = AudioFormat::I16;
    } else if (fmt.Format.wBitsPerSample == 24) {
      ret.sample_format = AudioFormat::I24;
    } else {
      return {};
    }
  } else if (fmt.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt.SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
    WORD bits_per_sample = fmt.Format.wBitsPerSample;
    WORD valid_bits = fmt.Samples.wValidBitsPerSample;
    if (bits_per_sample == 16) {
      ret.sample_format = AudioFormat::I16;
    } else if (bits_per_sample == 24 && valid_bits == 24) {
      ret.sample_format = AudioFormat::I24;
    } else if (bits_per_sample == 32 && valid_bits == 24) {
      ret.sample_format = AudioFormat::I24_X8;
    } else if (bits_per_sample == 32 && valid_bits == 32) {
      ret.sample_format = AudioFormat::I32;
    }
  } else if (
      fmt.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT &&
      fmt.Format.wBitsPerSample == 32 && fmt.Samples.wValidBitsPerSample == 32) {
    ret.sample_format = AudioFormat::F32;
  } else {
    return {};
  }

  switch (fmt.Format.nSamplesPerSec) {
    case 44100: ret.sample_rate = AudioDeviceSampleRate::Hz44100; break;
    case 48000: ret.sample_rate = AudioDeviceSampleRate::Hz48000; break;
    case 88200: ret.sample_rate = AudioDeviceSampleRate::Hz88200; break;
    case 96000: ret.sample_rate = AudioDeviceSampleRate::Hz96000; break;
    case 176400: ret.sample_rate = AudioDeviceSampleRate::Hz176400; break;
    case 192000: ret.sample_rate = AudioDeviceSampleRate::Hz192000; break;
    default: return {};
  }

  ret.num_channels = fmt.Format.nChannels;

  return ret;
}

//

AudioDeviceWASAPI2::~AudioDeviceWASAPI2() {
  if (impl_uid)
    CoTaskMemFree(impl_uid);
}

//

bool ActiveDeviceWASAPI2::open(IMMDevice* new_device) {
  ComPtr<IMMDevice> active_device(new_device);
  ComPtr<IAudioClient3> new_client;

  HRESULT result = active_device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&new_client);
  if (FAILED(result))
    return false;

  WAVEFORMATEXTENSIBLE* mix_format;
  new_client->GetMixFormat((WAVEFORMATEX**)&mix_format);

  new_client->GetSharedModeEnginePeriod(
      (WAVEFORMATEX*)mix_format,
      &default_low_latency_buffer_size,
      &low_latency_buffer_alignment,
      &min_low_latency_buffer_size,
      &max_low_latency_buffer_size);

  new_client->GetDevicePeriod(&default_device_period, &min_device_period);

  min_low_latency_period = buffer_size_to_period(min_low_latency_buffer_size, mix_format->Format.nSamplesPerSec);
  max_low_latency_period = buffer_size_to_period(max_low_latency_buffer_size, mix_format->Format.nSamplesPerSec);
  absolute_min_period = std::min(min_low_latency_period, min_device_period);
  device = active_device.Detach();
  client = new_client.Detach();
  shared_format = *mix_format;
  CoTaskMemFree(mix_format);
  return true;
}

void ActiveDeviceWASAPI2::close() {
  client->Release();
  device->Release();
  absolute_min_period = 0;
  device = nullptr;
  client = nullptr;
}

bool ActiveDeviceWASAPI2::init_stream(
    bool exclusive_mode,
    AudioDevicePeriod period,
    AudioFormat sample_format,
    AudioDeviceSampleRate sample_rate,
    uint32_t num_stream) {
  assert(device && client);

  DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  auto sample_rate_value = compatible_sample_rates[(uint32_t)sample_rate];
  AUDCLNT_SHAREMODE share_mode = exclusive_mode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
  uint32_t channel_mask = (1u << num_stream) - 1u;

  AudioClientProperties properties{};
  properties.cbSize = sizeof(AudioClientProperties);
  properties.eCategory = AudioCategory_Media;
  properties.Options |= AUDCLNT_STREAMOPTIONS_RAW;
  properties.Options |= AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
  if (FAILED(client->SetClientProperties(&properties)))
    return false;

  if (exclusive_mode) {
    WAVEFORMATEXTENSIBLE waveformat = make_waveformat(sample_format, sample_rate_value.first, num_stream, channel_mask);
    HRESULT result = client->Initialize(share_mode, stream_flags, period, period, (const WAVEFORMATEX*)&waveformat, nullptr);
    if (FAILED(result)) {
      return false;
    }
  } else {
    uint32_t buffer_size = period_to_buffer_size(period, sample_rate_value.first);
    WAVEFORMATEXTENSIBLE waveformat = shared_format;
    waveformat.Format.nChannels = num_stream;
    waveformat.dwChannelMask = channel_mask;
    if (math::in_range_inclusive(buffer_size, min_low_latency_buffer_size, max_low_latency_buffer_size) &&
        math::is_multiple_of(buffer_size, low_latency_buffer_alignment)) {
      // Use low-latency shared mode
      HRESULT result =
          client->InitializeSharedAudioStream(stream_flags, buffer_size, (const WAVEFORMATEX*)&waveformat, nullptr);
      if (FAILED(result)) {
        return false;
      }
    } else {
      HRESULT result = client->Initialize(share_mode, stream_flags, period, 0, (const WAVEFORMATEX*)&waveformat, nullptr);
      if (FAILED(result)) {
        return false;
      }
    }
  }

  event = CreateEvent(nullptr, FALSE, FALSE, L"WB_OUTPUT_STREAM_EVENT");
  client->SetEventHandle(event);
  return true;
}

void ActiveDeviceWASAPI2::stop_stream() {
  CloseHandle(event);
}

//

SessionEventWASAPI::SessionEventWASAPI(AudioIOWASAPI2* audio_io) : io(audio_io) {
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::QueryInterface(REFIID riid, void __RPC_FAR* __RPC_FAR* ppvObject) {
  if (ppvObject == nullptr)
    return E_POINTER;
  if (riid == __uuidof(IAudioSessionEvents))
    *ppvObject = static_cast<IAudioSessionEvents*>(this);
  else
    return E_NOINTERFACE;
  return S_OK;
}

ULONG STDMETHODCALLTYPE SessionEventWASAPI::AddRef(void) {
  return 1000;
}

ULONG STDMETHODCALLTYPE SessionEventWASAPI::Release(void) {
  return 1000;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnChannelVolumeChanged(
    DWORD ChannelCount,
    float NewChannelVolumeArray[],
    DWORD ChangedChannel,
    LPCGUID EventContext) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnStateChanged(AudioSessionState NewState) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE SessionEventWASAPI::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) {
  return S_OK;
}

//

DeviceNotificationWASAPI::DeviceNotificationWASAPI(AudioIOWASAPI2* audio_io) : io(audio_io) {
}

HRESULT STDMETHODCALLTYPE DeviceNotificationWASAPI::QueryInterface(REFIID riid, void __RPC_FAR* __RPC_FAR* ppvObject) {
  if (ppvObject == nullptr)
    return E_POINTER;
  if (riid == __uuidof(IMMNotificationClient))
    *ppvObject = static_cast<IMMNotificationClient*>(this);
  else
    return E_NOINTERFACE;
  return S_OK;
}

ULONG STDMETHODCALLTYPE DeviceNotificationWASAPI::AddRef(void) {
  return 1000;
}

ULONG STDMETHODCALLTYPE DeviceNotificationWASAPI::Release(void) {
  return 1000;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationWASAPI::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
  bool should_reset_device = false;
  if (io->is_device_open()) {
    AudioDeviceID id = make_device_id_hash(pwstrDeviceId);
    if (id == io->current_output_device_id || id == io->current_input_device_id) {
      if (has_bit(dwNewState, DEVICE_STATE_DISABLED, DEVICE_STATE_NOTPRESENT, DEVICE_STATE_UNPLUGGED)) {
        should_reset_device = true;
      }
    }
  }

  auto& callback = io->device_removed_listener_fn;
  if (callback.first)
    callback.first(callback.second, should_reset_device);

  char id[128]{};
  wcstombs_s(nullptr, id, pwstrDeviceId, wcslen(pwstrDeviceId));
  Log::debug("Device state changed: {} {}", id, dwNewState);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationWASAPI::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
  char id[128]{};
  wcstombs_s(nullptr, id, pwstrDeviceId, wcslen(pwstrDeviceId));
  Log::debug("Device added: {}", id);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationWASAPI::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
  char id[128]{};
  wcstombs_s(nullptr, id, pwstrDeviceId, wcslen(pwstrDeviceId));
  Log::debug("Device removed: {}", id);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
DeviceNotificationWASAPI::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
  if ((flow == EDataFlow::eRender || flow == EDataFlow::eCapture) && role == ERole::eMultimedia) {
    char id[128]{};
    wcstombs_s(nullptr, id, pwstrDefaultDeviceId, wcslen(pwstrDefaultDeviceId));
    Log::debug("Default device changed: {} {} {}", (int)flow, (int)role, id);
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationWASAPI::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
  char id[128]{};
  wcstombs_s(nullptr, id, pwstrDeviceId, wcslen(pwstrDeviceId));
  Log::debug("Device property changed {}", id);

  if (key.fmtid == PKEY_AudioEngine_DeviceFormat.fmtid) {
    auto& callback = io->device_format_changed_listener_fn;
    Log::debug("PKEY_AudioEngine_DeviceFormat changed");
    if (callback.first)
      callback.first(callback.second);
  } else {
    wchar_t* key_id;
    StringFromCLSID(key.fmtid, &key_id);
    wcstombs_s(nullptr, id, key_id, wcslen(key_id));
    CoTaskMemFree(key_id);
    Log::debug("{} changed: {}", id, key.pid);
  }

  return S_OK;
}

//

AudioIOWASAPI2::~AudioIOWASAPI2() {
  input_devices.clear();
  output_devices.clear();
  device_enumerator->UnregisterEndpointNotificationCallback(&device_notification_handler);
  if (device_enumerator)
    device_enumerator->Release();
}

bool AudioIOWASAPI2::init() {
  HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
  if (FAILED(result))
    return false;
  shared_mode_support = true;
  exclusive_mode_support = true;
  device_enumerator->RegisterEndpointNotificationCallback(&device_notification_handler);
  return rescan_device();
}

bool AudioIOWASAPI2::scan_audio_endpoints(EDataFlow type, Vector<AudioDeviceWASAPI2>& device_list) {
  ComPtr<IMMDeviceCollection> device_collection;
  if (FAILED(device_enumerator->EnumAudioEndpoints(type, DEVICE_STATE_ACTIVE, &device_collection)))
    return false;

  uint32_t count = 0;
  device_collection->GetCount(&count);
  if (count == 0)
    return true;

  wchar_t* default_device_id;
  ComPtr<IMMDevice> default_device;
  device_enumerator->GetDefaultAudioEndpoint(type, ERole::eMultimedia, &default_device);
  default_device->GetId(&default_device_id);
  defer(CoTaskMemFree(default_device_id));

  std::wstring_view default_device_id_str(default_device_id);
  AudioDeviceType device_type;
  switch (type) {
    case EDataFlow::eCapture:
      device_type = AudioDeviceType::Input;
      num_input_device = count;
      break;
    case EDataFlow::eRender:
      device_type = AudioDeviceType::Output;
      num_output_device = count;
      break;
    default: return false;
  }

  Vector<AudioDeviceWASAPI2> endpoints;
  for (uint32_t i = 0; i < count; i++) {
    ComPtr<IMMDevice> device;
    if (FAILED(device_collection->Item(i, &device)))
      continue;

    ComPtr<IPropertyStore> props;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props)))
      continue;

    PROPVARIANT var_name;
    PropVariantInit(&var_name);
    if (FAILED(props->GetValue(PKEY_Device_FriendlyName, &var_name)))
      continue;
    defer(PropVariantClear(&var_name));

    PROPVARIANT var_driver;
    PropVariantInit(&var_driver);
    if (FAILED(props->GetValue(PKEY_Device_ContainerId, &var_driver)))
      continue;
    defer(PropVariantClear(&var_driver));

    ComPtr<IAudioClient3> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&client)))
      continue;

    wchar_t* device_id;
    if (FAILED(device->GetId(&device_id)))
      continue;

    std::wstring_view device_id_str(device_id);
    AudioDeviceWASAPI2& endpoint = endpoints.emplace_back();
    wcstombs_s(nullptr, endpoint.properties.name, var_name.pwszVal, sizeof(endpoint.properties.name));
    endpoint.properties.id = make_device_id_hash(device_id_str);
    endpoint.properties.type = device_type;
    endpoint.properties.io_type = AudioIOType::WASAPI;
    endpoint.container_id = *var_driver.puuid;
    endpoint.impl_uid = device_id;

    // Mark this device as the default
    if (device_id_str == default_device_id_str) {
      switch (type) {
        case EDataFlow::eCapture: default_input_device = endpoint.properties; break;
        case EDataFlow::eRender: default_output_device = endpoint.properties; break;
        default: WB_UNREACHABLE();
      }
    }
  }

  device_list = std::move(endpoints);

  return true;
}

uint32_t AudioIOWASAPI2::find_device_index(AudioDeviceType type, AudioDeviceID id) const {
  const Vector<AudioDeviceWASAPI2>& devices = (type == AudioDeviceType::Input) ? input_devices : output_devices;
  uint32_t idx = 0;
  bool found = false;
  for (const auto& device : devices) {
    if (device.properties.id == id) {
      found = true;
      break;
    }
    idx++;
  }
  if (!found)
    return WB_INVALID_AUDIO_DEVICE_INDEX;
  return idx;
}

bool AudioIOWASAPI2::rescan_device() {
  scan_audio_endpoints(EDataFlow::eCapture, input_devices);
  scan_audio_endpoints(EDataFlow::eRender, output_devices);
  return true;
}

uint32_t AudioIOWASAPI2::get_input_device_index(AudioDeviceID id) const {
  return find_device_index(AudioDeviceType::Input, id);
}

uint32_t AudioIOWASAPI2::get_output_device_index(AudioDeviceID id) const {
  return find_device_index(AudioDeviceType::Output, id);
}

const AudioDeviceProperties& AudioIOWASAPI2::get_input_device_properties(uint32_t device_idx) const {
  return input_devices[device_idx].properties;
}

const AudioDeviceProperties& AudioIOWASAPI2::get_output_device_properties(uint32_t device_idx) const {
  return output_devices[device_idx].properties;
}

bool AudioIOWASAPI2::is_stream_running() const {
  return running.load(std::memory_order_relaxed);
}

bool AudioIOWASAPI2::is_on_the_same_driver(
    AudioDeviceType a_type,
    uint32_t a_device,
    AudioDeviceType b_type,
    uint32_t b_device) const {
  return false;
}

bool AudioIOWASAPI2::open_device(uint32_t input_device_idx, uint32_t output_device_idx) {
  if (output_device_idx == WB_INVALID_AUDIO_DEVICE_INDEX)
    return false;

  ComPtr<IMMDevice> input_device;
  ComPtr<IMMDevice> output_device;
  AudioDeviceProperties input_device_props{};
  AudioDeviceProperties output_device_props{};
  {
    if (input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX) {
      if (input_device_idx >= input_devices.size())
        return false;
      const AudioDeviceWASAPI2& device = input_devices[input_device_idx];
      input_device_props = device.properties;
      if (FAILED(device_enumerator->GetDevice(device.impl_uid, &input_device)))
        return false;
    }

    if (output_device_idx >= output_devices.size())
      return false;
    const AudioDeviceWASAPI2& device = output_devices[output_device_idx];
    output_device_props = device.properties;
    if (FAILED(device_enumerator->GetDevice(device.impl_uid, &output_device)))
      return false;
  }

  IMMDevice* input_device_unwrapped = input_device.Detach();
  IMMDevice* output_device_unwrapped = output_device.Detach();

  if (input_device_unwrapped) {
    if (!input.open(input_device_unwrapped)) {
      return false;
    }
  }

  if (!output.open(output_device_unwrapped)) {
    input.close();
    return false;
  }

  current_input_device_id = input_device_props.id;
  current_output_device_id = output_device_props.id;
  min_period = std::max(output.absolute_min_period, input.absolute_min_period);
  buffer_alignment = std::min(32u, std::max(output.low_latency_buffer_alignment, input.low_latency_buffer_alignment));

  constexpr int32_t max_channel_count = 32;
  for (auto smp_format : compatible_formats) {
    for (auto sample_rate : compatible_sample_rates) {
      for (int32_t channels = 0; channels < max_channel_count + 1; channels++) {
        uint32_t sample_rate_bit_mask = 1U << (uint32_t)sample_rate.second;
        uint32_t format_bit_mask = 1U << (uint32_t)smp_format;
        uint64_t channel_mask = (1ull << (uint64_t)channels) - 1ull;
        WAVEFORMATEXTENSIBLE format = make_waveformat(smp_format, sample_rate.first, channels, (uint32_t)channel_mask);
        bool output_format_supported =
            SUCCEEDED(output.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));
        bool input_format_supported =
            SUCCEEDED(input.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));

        if (output_format_supported) {
          exclusive_output_format_bit_flags |= format_bit_mask;
          exclusive_output_sample_rate_bit_flags |= sample_rate_bit_mask;
          exclusive_sample_rate_bit_flags |= sample_rate_bit_mask;
          if (channels > max_input_channel_count) {
            max_input_channel_count = channels;
            input_channel_mask = (uint32_t)channel_mask;
          }
        }

        if (input_format_supported) {
          exclusive_input_format_bit_flags |= format_bit_mask;
          exclusive_input_sample_rate_bit_flags |= sample_rate_bit_mask;
          exclusive_sample_rate_bit_flags |= sample_rate_bit_mask;
          if (channels > max_output_channel_count) {
            max_output_channel_count = channels;
            output_channel_mask = (uint32_t)channel_mask;
          }
        }

        if (sample_rate.first == output.shared_format.Format.nSamplesPerSec) {
          shared_mode_sample_rate = sample_rate.second;
        }

        if (format.SubFormat == output.shared_format.SubFormat) {
          shared_mode_output_format = smp_format;
        }

        if (format.SubFormat == input.shared_format.SubFormat) {
          shared_mode_input_format = smp_format;
        }
      }
    }
  }

  open = true;
  return true;
}

void AudioIOWASAPI2::close_device() {
  if (!open)
    return;
  if (running) {
    running = false;
    audio_thread.join();
    capture_client->Release();
    render_client->Release();
    output.stop_stream();
    input.stop_stream();
  }
  output.close();
  input.close();
  open = false;
  min_period = 0;
  buffer_alignment = 0;
  max_input_channel_count = 0;
  max_output_channel_count = 0;
  current_input_format = {};
  current_output_format = {};
}

bool AudioIOWASAPI2::start(
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

  uint32_t sample_rate_value = get_sample_rate_value(sample_rate);
  AudioDevicePeriod period = buffer_size_to_period(buffer_size, sample_rate_value);
  if (!output.init_stream(exclusive_mode, period, output_format, sample_rate, num_input_channels))
    return false;

  if (input.device && input.client) {
    if (!input.init_stream(exclusive_mode, period, input_format, sample_rate, num_output_channels)) {
      output.close();
      return false;
    }
    input.client->GetBufferSize(&maximum_input_buffer_size);
    input.client->GetService(IID_PPV_ARGS(&capture_client));
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

  output.client->GetBufferSize(&maximum_output_buffer_size);
  output.client->GetService(IID_PPV_ARGS(&render_client));
  stream_sample_rate = (double)get_sample_rate_value(sample_rate);
  stream_buffer_size = buffer_size;
  stream_period = period;
  stream_fn = stream_callback_fn;
  running = true;
  audio_thread = std::thread(audio_thread_runner, this, priority);

  return true;
}

void AudioIOWASAPI2::audio_thread_runner(AudioIOWASAPI2* io, AudioThreadPriority priority) {
  IAudioCaptureClient* capture = io->capture_client;
  IAudioRenderClient* render = io->render_client;
  IAudioClient* input_client = io->input.client;
  IAudioClient* output_client = io->output.client;

#ifndef _NDEBUG
  SetThreadDescription(GetCurrentThread(), L"Whitebox Audio Thread");
#endif

  DWORD task_index = 0;
  HANDLE task = AvSetMmThreadCharacteristics(L"Pro Audio", &task_index);
  if (task) {
    AVRT_PRIORITY avrt_priority;
    switch (priority) {
      case AudioThreadPriority::Lowest: avrt_priority = AVRT_PRIORITY_VERYLOW; break;
      case AudioThreadPriority::Low: avrt_priority = AVRT_PRIORITY_LOW; break;
      case AudioThreadPriority::Normal: avrt_priority = AVRT_PRIORITY_NORMAL; break;
      case AudioThreadPriority::High: avrt_priority = AVRT_PRIORITY_HIGH; break;
      case AudioThreadPriority::Highest: avrt_priority = AVRT_PRIORITY_CRITICAL; break;
    }
    AvSetMmThreadPriority(task, avrt_priority);
  }

  uint32_t buffer_size = io->stream_buffer_size;
  uint32_t maximum_input_buffer_size = io->maximum_input_buffer_size;
  uint32_t maximum_output_buffer_size = io->maximum_output_buffer_size;
  AudioBuffer<float> input_buffer(buffer_size, io->current_input_format.num_channels);
  AudioBuffer<float> output_buffer(buffer_size, io->current_output_format.num_channels);

  // Buffer for audio capture queue
  AudioFormat input_sample_format = io->current_input_format.sample_format;
  AudioFormat output_sample_format = io->current_output_format.sample_format;
  uint32_t frame_size = get_audio_format_size(input_sample_format) * input_buffer.n_channels;
  uint32_t input_buffer_capacity = maximum_input_buffer_size + buffer_size;
  uint32_t input_buffer_byte_size = input_buffer_capacity * frame_size;
  BYTE* input_queue_buffer = (BYTE*)std::malloc(input_buffer_byte_size);
  uint32_t input_buffer_read_pos = 0;
  uint32_t input_buffer_write_pos = 0;
  uint32_t input_buffer_size = 0;
  assert(input_queue_buffer != nullptr && "Cannot allocate buffer for audio buffer queue");

  input_client->Start();
  output_client->Start();

  bool device_removed = false;
  double sample_rate = io->stream_sample_rate;
  HANDLE input_stream_event = io->input.event;
  HANDLE output_stream_event = io->output.event;
  AudioStreamFn stream_fn = io->stream_fn;

  // Pre-fill buffer
  BYTE* prefill;
  HRESULT hr = 0;
  render->GetBuffer(maximum_output_buffer_size, &prefill);
  render->ReleaseBuffer(maximum_output_buffer_size, AUDCLNT_BUFFERFLAGS_SILENT);
  assert(input_sample_format == AudioFormat::F32);

  while (io->running.load(std::memory_order_relaxed)) {
    // Read queued samples
    if (input_buffer_size > 0) {
      uint32_t read_count = math::min(buffer_size, input_buffer_size);
      uint32_t begin_read = input_buffer_read_pos;
      uint32_t end_read = (input_buffer_read_pos + read_count) % input_buffer_capacity;
#if LOG_BUFFERING
      Log::debug("Read: {} {} {}", read_count, input_buffer_read_pos, end_read);
#endif
      if (begin_read <= end_read) {
        void* src = input_queue_buffer + (input_buffer_read_pos * frame_size);
        input_buffer.deinterleave_samples_from(src, 0, read_count, input_sample_format);
        input_buffer_read_pos = end_read;
        input_buffer_size -= read_count;
      } else {
        uint32_t read_offset = input_buffer_capacity - input_buffer_read_pos;
        input_buffer.deinterleave_samples_from(
            input_queue_buffer + (input_buffer_read_pos * frame_size), 0, read_offset, input_sample_format);
        input_buffer.deinterleave_samples_from(input_queue_buffer, read_offset, end_read, input_sample_format);
        input_buffer_read_pos = end_read;
        input_buffer_size -= read_count;
      }
#if LOG_BUFFERING
      Log::debug("Input buffer size: {}", input_buffer_size);
#endif
    }

    stream_fn(output_buffer, input_buffer, sample_rate);
    // engine->process(input_buffer, output_buffer, sample_rate);

#if LOG_BUFFERING
    Log::debug("Splitting buffer");
#endif

    // WASAPI may use the default device buffer size instead of the requested buffer size. If
    // that's the case, we have to split the buffer manually so that it fits into the default
    // device buffer size.
    uint32_t output_offset = 0;
    hr = 0;
    while (output_offset < output_buffer.n_samples) {
      DWORD wait_result = WaitForSingleObject(input_stream_event, INFINITE);
      if (wait_result == WAIT_TIMEOUT)
        break;

      // Fetch next input buffer
      BYTE* buffer;
      DWORD flags;
      uint32_t frames_available;
      while (SUCCEEDED(capture->GetBuffer(&buffer, &frames_available, &flags, nullptr, nullptr)) && frames_available > 0) {
        uint32_t available_size = input_buffer_capacity - input_buffer_size;
        if (frames_available > available_size) {
          capture->ReleaseBuffer(0);
          break;
        }
        uint32_t begin_write = input_buffer_write_pos;
        uint32_t end_write = (input_buffer_write_pos + frames_available) % input_buffer_capacity;
#if LOG_BUFFERING
        Log::info("Write: {} {} {}", frames_available, input_buffer_write_pos, end_write);
#endif
        // Enqueue captured samples
        if (begin_write <= end_write) {
          void* dst = input_queue_buffer + (input_buffer_write_pos * frame_size);
          if (has_bit(flags, AUDCLNT_BUFFERFLAGS_SILENT)) [[unlikely]]
            std::memset(dst, 0, frames_available * frame_size);
          else
            std::memcpy(dst, buffer, frames_available * frame_size);
          input_buffer_write_pos = end_write;
          input_buffer_size += frames_available;
        } else {
          if (has_bit(flags, AUDCLNT_BUFFERFLAGS_SILENT)) [[unlikely]] {
            std::memset(
                input_queue_buffer + (input_buffer_write_pos * frame_size),
                0,
                (input_buffer_capacity - begin_write) * frame_size);
            std::memset(input_queue_buffer, 0, end_write * frame_size);
          } else {
            std::memcpy(
                input_queue_buffer + (input_buffer_write_pos * frame_size),
                buffer,
                (input_buffer_capacity - begin_write) * frame_size);
            std::memcpy(
                input_queue_buffer, buffer + (input_buffer_capacity - begin_write) * frame_size, end_write * frame_size);
          }
          input_buffer_write_pos = end_write;
          input_buffer_size += frames_available;
        }
        hr = capture->ReleaseBuffer(frames_available);
      }

      uint32_t padding;
      hr = output_client->GetCurrentPadding(&padding);
      if (!SUCCEEDED(hr)) {
        break;
      }

      frames_available = maximum_output_buffer_size - padding;
      if (frames_available > (output_buffer.n_samples - output_offset))
        frames_available = output_buffer.n_samples - output_offset;

      if (frames_available == 0) {
        WaitForSingleObject(output_stream_event, INFINITE);
        continue;
      }

      hr = render->GetBuffer(frames_available, (BYTE**)&buffer);
      if (!SUCCEEDED(hr)) {
        break;
      }

      // Write interleaved sample to output buffer
      output_buffer.interleave_samples_to(buffer, output_offset, frames_available, output_sample_format);

      hr = render->ReleaseBuffer(frames_available, 0);
      if (!SUCCEEDED(hr)) {
        break;
      }

      output_offset += frames_available;
    }

    if (!SUCCEEDED(hr)) {
      break;
    }
  }

  std::free(input_queue_buffer);
  input_client->Stop();
  output_client->Stop();
}

AudioIO2* create_audio_io_wasapi() {
  AudioIOWASAPI2* audio_io = new (std::nothrow) AudioIOWASAPI2();
  if (!audio_io)
    return nullptr;
  if (!audio_io->init()) {
    delete audio_io;
    return nullptr;
  }
  return static_cast<AudioIO2*>(audio_io);
}

}  // namespace wb

#else

namespace wb {

AudioIO2* create_audio_io_wasapi2() {
  return nullptr;
}

}  // namespace wb

#endif