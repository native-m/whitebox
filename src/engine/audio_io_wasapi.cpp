#include "audio_io.h"

#ifdef WB_PLATFORM_WINDOWS
#include "core/audio_buffer.h"
#include "core/audio_format_conv.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "engine/engine.h"
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <Audioclient.h>
#include <atomic>
#include <ks.h>
#include <ksmedia.h>
#include <wrl.h>

#ifndef __mmdeviceapi_h__
#include <initguid.h>
#include <mmdeviceapi.h>
#endif

#include <Functiondiscoverykeys_devpkey.h>
#include <avrt.h>

#define LOG_BUFFERING 0

#ifdef NDEBUG
#undef LOG_BUFFERING
#endif

using namespace Microsoft::WRL;

namespace wb {

struct AudioIOWASAPI;

struct FormatBitSizes {
    GUID subtype;
    WORD bits_per_sample;
    WORD valid_bits_per_sample;
};

inline static FormatBitSizes get_bit_sizes(AudioFormat audio_format) {
    switch (audio_format) {
        case AudioFormat::I8:
            return {KSDATAFORMAT_SUBTYPE_PCM, 8, 8};
        case AudioFormat::I16:
            return {KSDATAFORMAT_SUBTYPE_PCM, 16, 16};
        case AudioFormat::I24:
            return {KSDATAFORMAT_SUBTYPE_PCM, 24, 24};
        case AudioFormat::I24_X8:
            return {KSDATAFORMAT_SUBTYPE_PCM, 32, 24};
        case AudioFormat::I32:
            return {KSDATAFORMAT_SUBTYPE_PCM, 32, 32};
        case AudioFormat::F32:
            return {KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 32, 32};
        default:
            break;
    }
    return {};
}

inline static WAVEFORMATEXTENSIBLE to_waveformatex(AudioFormat sample_format, uint32_t sample_rate, uint16_t channels,
                                                   uint32_t channel_mask) {
    // NOTE: Some drivers does not work with WAVEFORMATEXTENSIBLE!
    auto [format, bits, valid_bits] = get_bit_sizes(sample_format);
    WAVEFORMATEXTENSIBLE waveformat {};

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

inline static AudioDeviceID get_whitebox_device_id(LPCWSTR device_id) {
    std::wstring_view device_str_id(device_id);
    AudioDeviceID id = std::hash<std::wstring_view> {}(device_str_id);
    return id;
}

struct AudioDeviceWASAPI {
    AudioDeviceProperties properties;
    uint32_t collection_index;
};

struct EndpointNotificationWASAPI : public IMMNotificationClient {
    AudioIOWASAPI* io {};

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void __RPC_FAR* __RPC_FAR* ppvObject) override;

    ULONG STDMETHODCALLTYPE AddRef(void) override { return 1; }

    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
};

struct ActiveDeviceWASAPI {
    IMMDevice* device {};
    IAudioClient3* client {};
    WAVEFORMATEXTENSIBLE shared_format {};

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

    uint32_t channel_count = 0;
    HANDLE stream_event {};
    bool use_polling = false;

    bool open(IMMDevice* new_device);

    void close();

    bool init_stream(bool exclusive_mode, AudioDevicePeriod period, AudioFormat sample_format,
                     AudioDeviceSampleRate sample_rate);

    void stop_stream();
};

struct AudioIOWASAPI : public AudioIO {
    IMMDeviceEnumerator* device_enumerator;
    EndpointNotificationWASAPI endpoint_notification;
    std::vector<AudioDeviceWASAPI> output_devices;
    std::vector<AudioDeviceWASAPI> input_devices;
    ActiveDeviceWASAPI output;
    ActiveDeviceWASAPI input;
    IAudioRenderClient* render_client {};
    IAudioCaptureClient* capture_client {};
    uint32_t exclusive_output_sample_rate_bit_flags = 0;
    uint32_t exclusive_input_sample_rate_bit_flags = 0;
    mutable std::shared_mutex notification_mutex;

    AudioDevicePeriod stream_period {};
    uint32_t stream_buffer_size {};
    uint32_t maximum_input_buffer_size {};
    uint32_t maximum_output_buffer_size {};
    uint32_t input_channel_mask = 0;
    uint32_t output_channel_mask = 0;
    double stream_sample_rate {};
    AudioFormat input_stream_format {};
    AudioFormat output_stream_format {};
    Engine* current_engine;
    std::atomic_bool running;
    std::thread audio_thread;

    virtual ~AudioIOWASAPI();

    bool init();
    bool exclusive_mode_support() override { return false; }
    bool shared_mode_support() override { return true; }

    bool rescan_devices() override {
        input_devices.resize(0);
        output_devices.resize(0);
        return scan_audio_endpoints(EDataFlow::eCapture, input_devices) &&
               scan_audio_endpoints(EDataFlow::eRender, output_devices);
    }

    uint32_t get_input_device_index(AudioDeviceID id) const override { return find_device_index(input_devices, id); }

    uint32_t get_output_device_index(AudioDeviceID id) const override { return find_device_index(output_devices, id); }

    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        std::shared_lock lock(notification_mutex);
        return input_devices[idx].properties;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        std::shared_lock lock(notification_mutex);
        return output_devices[idx].properties;
    }

    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_id) override;

    void close_device() override;

    bool start(Engine* engine, bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate, AudioThreadPriority priority) override;

    bool scan_audio_endpoints(EDataFlow type, std::vector<AudioDeviceWASAPI>& endpoints);

    uint32_t find_device_index(const std::vector<AudioDeviceWASAPI>& devices, AudioDeviceID id) const;

    static void audio_thread_runner(AudioIOWASAPI* instance, AudioThreadPriority priority);
};

// ------------------------------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
        return E_POINTER;
    if (riid == __uuidof(IMMNotificationClient))
        *ppvObject = static_cast<IMMNotificationClient*>(this);
    else
        return E_NOINTERFACE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    AudioDeviceID id = get_whitebox_device_id(pwstrDeviceId);
    if (id == io->current_output_device_id || id == io->current_input_device_id) {
        if (has_bit(dwNewState, DEVICE_STATE_DISABLED, DEVICE_STATE_NOTPRESENT, DEVICE_STATE_UNPLUGGED)) {
            if (io->device_removed_cb) {
                io->device_removed_cb(nullptr);
            }
        }
    } else {
        std::unique_lock lock(io->notification_mutex);
        io->rescan_devices();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                                             LPCWSTR pwstrDefaultDeviceId) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationWASAPI::OnPropertyValueChanged(LPCWSTR pwstrDeviceId,
                                                                             const PROPERTYKEY key) {
    return S_OK;
}

//

bool ActiveDeviceWASAPI::open(IMMDevice* new_device) {
    ComPtr<IMMDevice> active_device(new_device);
    ComPtr<IAudioClient3> new_client;

    HRESULT result = active_device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&new_client);
    if (FAILED(result))
        return false;

    AudioClientProperties properties {};
    properties.cbSize = sizeof(AudioClientProperties);
    properties.eCategory = AudioCategory_Media;
    properties.Options |= AUDCLNT_STREAMOPTIONS_RAW;
    properties.Options |= AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
    new_client->SetClientProperties(&properties);

    WAVEFORMATEXTENSIBLE* mix_format;
    new_client->GetMixFormat((WAVEFORMATEX**)&mix_format);

    new_client->GetSharedModeEnginePeriod((WAVEFORMATEX*)mix_format, &default_low_latency_buffer_size,
                                          &low_latency_buffer_alignment, &min_low_latency_buffer_size,
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

void ActiveDeviceWASAPI::close() {
    client->Release();
    device->Release();
    device = nullptr;
    client = nullptr;
}

bool ActiveDeviceWASAPI::init_stream(bool exclusive_mode, AudioDevicePeriod period, AudioFormat sample_format,
                                     AudioDeviceSampleRate sample_rate) {
    assert(device && client);

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    auto sample_rate_value = compatible_sample_rates[(uint32_t)sample_rate];
    AUDCLNT_SHAREMODE share_mode = exclusive_mode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

    if (exclusive_mode) {
        WAVEFORMATEXTENSIBLE waveformat = to_waveformatex(sample_format, sample_rate_value.first, 2, (1u << 2) - 1u);
        HRESULT result =
            client->Initialize(share_mode, stream_flags, period, period, (const WAVEFORMATEX*)&waveformat, nullptr);
        if (FAILED(result)) {
            return false;
        }
    } else {
        uint32_t buffer_size = period_to_buffer_size(period, sample_rate_value.first);
        if (in_range(buffer_size, min_low_latency_buffer_size, max_low_latency_buffer_size) &&
            is_multiple_of(buffer_size, low_latency_buffer_alignment)) {
            // Use low-latency shared mode
            HRESULT result = client->InitializeSharedAudioStream(stream_flags, buffer_size,
                                                                 (const WAVEFORMATEX*)&shared_format, nullptr);
            if (FAILED(result)) {
                return false;
            }
        } else {
            HRESULT result =
                client->Initialize(share_mode, stream_flags, period, 0, (const WAVEFORMATEX*)&shared_format, nullptr);
            if (FAILED(result)) {
                return false;
            }
        }

        channel_count = shared_format.Format.nChannels;
    }

    stream_event = CreateEvent(nullptr, FALSE, FALSE, L"WB_OUTPUT_STREAM_EVENT");
    client->SetEventHandle(stream_event);

    return true;
}

void ActiveDeviceWASAPI::stop_stream() {
    CloseHandle(stream_event);
}

//

AudioIOWASAPI::~AudioIOWASAPI() {
    close_device();
    device_enumerator->UnregisterEndpointNotificationCallback(&endpoint_notification);
    device_enumerator->Release();
}

bool AudioIOWASAPI::init() {
    HRESULT result =
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
    if (FAILED(result))
        return false;
    endpoint_notification.io = this;
    device_enumerator->RegisterEndpointNotificationCallback(&endpoint_notification);
    return rescan_devices();
}

bool AudioIOWASAPI::open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_id) {
    Log::info("Opening audio devices...");

    if (output_device_id != 0) {
        ComPtr<IMMDeviceCollection> device_collection;
        device_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &device_collection);
        uint32_t device_index = find_device_index(output_devices, output_device_id);
        if (device_index == WB_INVALID_AUDIO_DEVICE_INDEX)
            return false;
        IMMDevice* device;
        AudioDeviceWASAPI& output_device = output_devices[device_index];
        device_collection->Item(output_device.collection_index, &device);
        if (!output.open(device))
            return false;
    }

    if (input_device_id != 0) {
        ComPtr<IMMDeviceCollection> device_collection;
        device_enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &device_collection);
        uint32_t device_index = find_device_index(input_devices, input_device_id);
        if (device_index == WB_INVALID_AUDIO_DEVICE_INDEX) {
            output.close();
            return false;
        }
        IMMDevice* device;
        AudioDeviceWASAPI& input_device = input_devices[device_index];
        device_collection->Item(input_device.collection_index, &device);
        if (!input.open(device)) {
            output.close();
            return false;
        }
    }

    current_input_device_id = input_device_id;
    current_output_device_id = output_device_id;
    min_period = std::max(output.absolute_min_period, input.absolute_min_period);

    // Maximum buffer alignment for low latency stream is 32
    buffer_alignment = std::min(32u, std::max(output.low_latency_buffer_alignment, input.low_latency_buffer_alignment));

    // Check all possible formats
    constexpr int32_t max_channel_count = 32;
    for (auto smp_format : compatible_formats) {
        for (auto sample_rate : compatible_sample_rates) {
            for (auto channels : std::ranges::iota_view(1, max_channel_count + 1)) {
                uint32_t sample_rate_bit_mask = 1U << (uint32_t)sample_rate.second;
                uint32_t format_bit_mask = 1U << (uint32_t)smp_format;
                uint64_t channel_mask = (1ull << (uint64_t)channels) - 1ull;
                WAVEFORMATEXTENSIBLE format =
                    to_waveformatex(smp_format, sample_rate.first, channels, (uint32_t)channel_mask);
                bool output_format_supported = SUCCEEDED(
                    output.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));
                bool input_format_supported = SUCCEEDED(
                    input.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));

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

void AudioIOWASAPI::close_device() {
    if (!open)
        return;
    Log::info("Closing audio devices...");
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
}

bool AudioIOWASAPI::start(Engine* engine, bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
                          AudioFormat output_format, AudioDeviceSampleRate sample_rate, AudioThreadPriority priority) {
    if (running)
        return false;

    uint32_t sample_rate_value = get_sample_rate_value(sample_rate);
    AudioDevicePeriod period = buffer_size_to_period(buffer_size, sample_rate_value);
    if (!output.init_stream(exclusive_mode, period, output_format, sample_rate))
        return false;
    if (!input.init_stream(exclusive_mode, period, input_format, sample_rate)) {
        output.close();
        return false;
    }

    input.client->GetBufferSize(&maximum_input_buffer_size);
    input.client->GetService(IID_PPV_ARGS(&capture_client));
    output.client->GetBufferSize(&maximum_output_buffer_size);
    output.client->GetService(IID_PPV_ARGS(&render_client));
    stream_sample_rate = (double)get_sample_rate_value(sample_rate);
    stream_buffer_size = buffer_size;
    stream_period = period;
    input_stream_format = input_format;
    output_stream_format = output_format;
    current_engine = engine;
    running = true;
    audio_thread = std::thread(audio_thread_runner, this, priority);

    return true;
}

bool AudioIOWASAPI::scan_audio_endpoints(EDataFlow type, std::vector<AudioDeviceWASAPI>& endpoints) {
    ComPtr<IMMDeviceCollection> device_collection;
    device_enumerator->EnumAudioEndpoints(type, DEVICE_STATE_ACTIVE, &device_collection);

    uint32_t count = 0;
    device_collection->GetCount(&count);

    wchar_t* default_device_id;
    ComPtr<IMMDevice> default_device;
    device_enumerator->GetDefaultAudioEndpoint(type, eConsole, &default_device);
    default_device->GetId(&default_device_id);

    std::wstring_view default_device_str_id(default_device_id);
    AudioDeviceType device_type;
    switch (type) {
        case EDataFlow::eCapture:
            device_type = AudioDeviceType::Input;
            input_device_count = count;
            break;
        case EDataFlow::eRender:
            device_type = AudioDeviceType::Output;
            output_device_count = count;
            break;
        default:
            WB_UNREACHABLE();
    }

    for (uint32_t i = 0; i < count; i++) {
        ComPtr<IMMDevice> device;
        device_collection->Item(i, &device);

        ComPtr<IPropertyStore> property_store;
        device->OpenPropertyStore(STGM_READ, &property_store);

        PROPVARIANT var_name;
        PropVariantInit(&var_name);
        property_store->GetValue(PKEY_Device_FriendlyName, &var_name);

        PROPVARIANT var_format;
        PropVariantInit(&var_format);
        property_store->GetValue(PKEY_AudioEngine_DeviceFormat, &var_format);
        WAVEFORMATEXTENSIBLE* waveformatex = (WAVEFORMATEXTENSIBLE*)var_format.blob.pBlobData;
        PropVariantClear(&var_format);

        wchar_t* device_id;
        device->GetId(&device_id);

        std::wstring_view device_str_id(device_id);
        AudioDeviceID id = std::hash<std::wstring_view> {}(device_str_id);
        AudioDeviceWASAPI& endpoint = endpoints.emplace_back();
        wcstombs_s(nullptr, endpoint.properties.name, var_name.pwszVal, sizeof(endpoint.properties.name));
        endpoint.properties.id = id;
        endpoint.properties.type = device_type;
        endpoint.properties.io_type = AudioIOType::WASAPI;
        endpoint.collection_index = i;

        // Mark this device as the default
        if (device_str_id == default_device_str_id) {
            switch (type) {
                case EDataFlow::eCapture:
                    default_input_device = endpoint.properties;
                    break;
                case EDataFlow::eRender:
                    default_output_device = endpoint.properties;
                    break;
                default:
                    WB_UNREACHABLE();
            }
        }

        PropVariantClear(&var_name);
        CoTaskMemFree(device_id);
    }

    CoTaskMemFree(default_device_id);
    return true;
}

uint32_t AudioIOWASAPI::find_device_index(const std::vector<AudioDeviceWASAPI>& devices, AudioDeviceID id) const {
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

void AudioIOWASAPI::audio_thread_runner(AudioIOWASAPI* instance, AudioThreadPriority priority) {
    IAudioCaptureClient* capture = instance->capture_client;
    IAudioRenderClient* render = instance->render_client;
    IAudioClient* input_client = instance->input.client;
    IAudioClient* output_client = instance->output.client;
    Engine* engine = instance->current_engine;

#ifndef _NDEBUG
    SetThreadDescription(GetCurrentThread(), L"Whitebox Audio Thread");
#endif

    DWORD task_index = 0;
    HANDLE task = AvSetMmThreadCharacteristics(L"Pro Audio", &task_index);
    if (task) {
        AVRT_PRIORITY avrt_priority;
        switch (priority) {
            case AudioThreadPriority::Lowest:
                avrt_priority = AVRT_PRIORITY_VERYLOW;
                break;
            case AudioThreadPriority::Low:
                avrt_priority = AVRT_PRIORITY_LOW;
                break;
            case AudioThreadPriority::Normal:
                avrt_priority = AVRT_PRIORITY_NORMAL;
                break;
            case AudioThreadPriority::High:
                avrt_priority = AVRT_PRIORITY_HIGH;
                break;
            case AudioThreadPriority::Highest:
                avrt_priority = AVRT_PRIORITY_CRITICAL;
                break;
        }
        AvSetMmThreadPriority(task, avrt_priority);
    }

    uint32_t buffer_size = instance->stream_buffer_size;
    uint32_t maximum_input_buffer_size = instance->maximum_input_buffer_size;
    uint32_t maximum_output_buffer_size = instance->maximum_output_buffer_size;
    AudioBuffer<float> input_buffer(buffer_size, instance->input.channel_count);
    AudioBuffer<float> output_buffer(buffer_size, instance->output.channel_count);

    // Buffer for audio capture queue
    uint32_t frame_size = get_audio_format_size(instance->input_stream_format) * input_buffer.n_channels;
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
    double sample_rate = instance->stream_sample_rate;
    HANDLE input_stream_event = instance->input.stream_event;
    HANDLE output_stream_event = instance->output.stream_event;

    // Pre-fill buffer
    BYTE* prefill;
    HRESULT hr = 0;
    render->GetBuffer(maximum_output_buffer_size, &prefill);
    render->ReleaseBuffer(maximum_output_buffer_size, AUDCLNT_BUFFERFLAGS_SILENT);
    assert(instance->input_stream_format == AudioFormat::F32);

    while (instance->running.load(std::memory_order_relaxed)) {
        if (input_buffer_size > 0) {
            uint32_t read_count = math::min(buffer_size, input_buffer_size);
            uint32_t begin_read = input_buffer_read_pos;
            uint32_t end_read = (input_buffer_read_pos + read_count) % input_buffer_capacity;
#if LOG_BUFFERING
            Log::debug("Read: {} {} {}", read_count, input_buffer_read_pos, end_read);
#endif
            if (begin_read <= end_read) {
                void* src = input_queue_buffer + (input_buffer_read_pos * frame_size);
                input_buffer.deinterleave_samples_from(src, 0, read_count, instance->input_stream_format);
                input_buffer_read_pos = end_read;
                input_buffer_size -= read_count;
            } else {
                uint32_t read_offset = input_buffer_capacity - input_buffer_read_pos;
                input_buffer.deinterleave_samples_from(input_queue_buffer + (input_buffer_read_pos * frame_size), 0,
                                                       read_offset, instance->input_stream_format);
                input_buffer.deinterleave_samples_from(input_queue_buffer, read_offset, end_read,
                                                       instance->input_stream_format);
                input_buffer_read_pos = end_read;
                input_buffer_size -= read_count;
            }
#if LOG_BUFFERING
            Log::debug("Input buffer size: {}", input_buffer_size);
#endif
        }

        engine->process(input_buffer, output_buffer, sample_rate);

#if LOG_BUFFERING
        Log::debug("Splitting buffer");
#endif

        // WASAPI may use the default device buffer size instead of user-defined buffer size. If
        // that's the case, we have to roll the buffer manually so that it fits into the default
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
            while (SUCCEEDED(capture->GetBuffer(&buffer, &frames_available, &flags, nullptr, nullptr)) &&
                   frames_available > 0) {
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
                        std::memset(input_queue_buffer + (input_buffer_write_pos * frame_size), 0,
                                    (input_buffer_capacity - begin_write) * frame_size);
                        std::memset(input_queue_buffer, 0, end_write * frame_size);
                    } else {
                        std::memcpy(input_queue_buffer + (input_buffer_write_pos * frame_size), buffer,
                                    (input_buffer_capacity - begin_write) * frame_size);
                        std::memcpy(input_queue_buffer, buffer + (input_buffer_capacity - begin_write) * frame_size,
                                    end_write * frame_size);
                    }
                    input_buffer_write_pos = end_write;
                    input_buffer_size += frames_available;
                }
                hr = capture->ReleaseBuffer(frames_available);
            }

            uint32_t padding;
            hr = output_client->GetCurrentPadding(&padding);
            if (!SUCCEEDED(hr)) [[unlikely]] {
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
            if (!SUCCEEDED(hr)) [[unlikely]] {
                break;
            }

            output_buffer.interleave_samples_to(buffer, output_offset, frames_available,
                                                instance->output_stream_format);

            hr = render->ReleaseBuffer(frames_available, 0);
            if (!SUCCEEDED(hr)) [[unlikely]] {
                break;
            }

            output_offset += frames_available;
        }

        if (!SUCCEEDED(hr)) [[unlikely]] {
            break;
        }
    }

    std::free(input_queue_buffer);
    input_client->Stop();
    output_client->Stop();
}

AudioIO* create_audio_io_wasapi() {
    AudioIOWASAPI* audio_io = new (std::nothrow) AudioIOWASAPI();
    if (!audio_io)
        return nullptr;
    if (!audio_io->init())
        return nullptr;
    return audio_io;
}

} // namespace wb

#else

#include "audio_io.h"

namespace wb {
AudioIO* create_audio_io_wasapi() {
    return nullptr;
}
} // namespace wb

#endif
