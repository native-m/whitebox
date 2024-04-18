#ifdef WIN32

#include "audio_io.h"
#include "core/debug.h"
#include "core/math.h"

#define WIN32_LEAN_AND_MEAN
#include <Audioclient.h>
#include <atomic>
#include <ks.h>
#include <ksmedia.h>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <wrl.h>

#ifndef __mmdeviceapi_h__
#include <initguid.h>
#include <mmdeviceapi.h>
#endif

#include <Functiondiscoverykeys_devpkey.h>

#define WB_INVALID_INDEX (~0U)

using namespace Microsoft::WRL;

namespace wb {

static const std::pair<uint32_t, AudioDeviceSampleRate> compatible_sample_rates[] = {
    {44100, AudioDeviceSampleRate::Hz44100},   {48000, AudioDeviceSampleRate::Hz48000},
    {88200, AudioDeviceSampleRate::Hz88200},   {96000, AudioDeviceSampleRate::Hz96000},
    {176400, AudioDeviceSampleRate::Hz176400}, {192000, AudioDeviceSampleRate::Hz192000},
};

static const AudioFormat compatible_formats[] = {
    AudioFormat::I16, AudioFormat::I24, AudioFormat::I24_X8, AudioFormat::I32, AudioFormat::F32,
};

static const uint16_t compatible_channel_count[] = {
    2,
};

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

inline static WAVEFORMATEXTENSIBLE to_waveformatex(AudioFormat sample_format, uint32_t sample_rate,
                                                   uint16_t channels) {
    // NOTE: Some drivers does not work with WAVEFORMATEXTENSIBLE!
    auto [format, bits, valid_bits] = get_bit_sizes(sample_format);
    WAVEFORMATEXTENSIBLE waveformat {};

    if (bits <= 16) {
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
    waveformat.dwChannelMask = (1u << channels) - 1;

    return waveformat;
}

inline uint32_t to_sample_count(REFERENCE_TIME ref_time, uint32_t sample_rate) {
    constexpr double sec_in_100_ns_units = 10000000.0;
    return (uint32_t)std::round(sample_rate * ref_time / sec_in_100_ns_units);
}

inline REFERENCE_TIME to_period(uint32_t buffer_size, uint32_t sample_rate) {
    constexpr double sec_in_100_ns_units = 10000000.0;
    return (REFERENCE_TIME)std::round(sec_in_100_ns_units * (buffer_size / (double)sample_rate));
}

struct AudioDeviceWASAPI {
    AudioDeviceProperties properties;
    ComPtr<IMMDevice> device;
};

struct ActiveDeviceWASAPI {
    IMMDevice* device {};
    IAudioClient3* client {};
    WAVEFORMATEXTENSIBLE shared_format {};
    std::vector<uint32_t> shared_buffer_sizes {};

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

    std::atomic<uint32_t> ato;

    bool open(ComPtr<IMMDevice>& new_device) {
        ComPtr<IMMDevice> active_device(new_device);
        ComPtr<IAudioClient3> new_client;

        HRESULT result = active_device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                                                 (void**)&new_client);

        if (FAILED(result))
            return false;

        AudioClientProperties properties {};
        properties.cbSize = sizeof(AudioClientProperties);
        properties.eCategory = AudioCategory_Media;
        properties.Options |= AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
        properties.Options |= AUDCLNT_STREAMOPTIONS_RAW;

        new_client->SetClientProperties(&properties);

        WAVEFORMATEXTENSIBLE* mix_format;
        new_client->GetMixFormat((WAVEFORMATEX**)&mix_format);

        new_client->GetSharedModeEnginePeriod(
            (WAVEFORMATEX*)mix_format, &default_low_latency_buffer_size,
            &low_latency_buffer_alignment, &min_low_latency_buffer_size,
            &max_low_latency_buffer_size);

        new_client->GetDevicePeriod(&default_device_period, &min_device_period);

        min_low_latency_period =
            buffer_size_to_period(min_low_latency_buffer_size, mix_format->Format.nSamplesPerSec);
        max_low_latency_period =
            buffer_size_to_period(max_low_latency_buffer_size, mix_format->Format.nSamplesPerSec);

        absolute_min_period = std::min(min_low_latency_period, min_device_period);

        uint32_t buffer_size = min_low_latency_buffer_size;
        shared_buffer_sizes.push_back(buffer_size);
        buffer_size += low_latency_buffer_alignment;
        while (buffer_size <= max_low_latency_buffer_size) {
            shared_buffer_sizes.push_back(buffer_size);
            buffer_size += low_latency_buffer_alignment;
        }

        while (buffer_size <= 8192) {
            if (!is_pow_2(buffer_size)) {
                buffer_size += low_latency_buffer_alignment;
                continue;
            }
            shared_buffer_sizes.push_back(buffer_size);
            buffer_size += low_latency_buffer_alignment;
        }

        device = active_device.Detach();
        client = new_client.Detach();
        shared_format = *mix_format;
        CoTaskMemFree(mix_format);

        return true;
    }

    void close() {
        client->Release();
        device->Release();
        device = nullptr;
        client = nullptr;
    }

    bool init_stream(bool exclusive_mode, AudioDevicePeriod period, AudioFormat sample_format,
                     AudioDeviceSampleRate sample_rate) {
        DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        auto sample_rate_value = compatible_sample_rates[(uint32_t)sample_rate];
        AUDCLNT_SHAREMODE share_mode =
            exclusive_mode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

        if (exclusive_mode) {
            WAVEFORMATEXTENSIBLE waveformat =
                to_waveformatex(sample_format, sample_rate_value.first, 2);
        } else {
            uint32_t buffer_size = period_to_buffer_size(period, sample_rate_value.first);
            if (in_range(buffer_size, min_low_latency_buffer_size, max_low_latency_buffer_size) &&
                is_multiple_of(buffer_size, low_latency_buffer_alignment)) {
                // Use low-latency mode

            } else {
            }
        }

        return true;
    }
};

struct AudioIOWASAPI : public AudioIO {
    std::vector<AudioDeviceWASAPI> output_devices;
    std::vector<AudioDeviceWASAPI> input_devices;
    ActiveDeviceWASAPI output;
    ActiveDeviceWASAPI input;
    uint32_t exclusive_output_sample_rate_mask = 0;
    uint32_t exclusive_input_sample_rate_mask = 0;

    virtual ~AudioIOWASAPI() { close_device(); }

    bool init() { return rescan_devices(); }

    bool rescan_devices() override {
        return scan_audio_endpoints(EDataFlow::eCapture, input_devices) &&
               scan_audio_endpoints(EDataFlow::eRender, output_devices);
    }

    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return input_devices[idx].properties;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return output_devices[idx].properties;
    }

    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_id) override {
        Log::info("Opening audio devices...");

        if (output_device_id != 0) {
            uint32_t device_index = find_device_index_(output_devices, output_device_id);
            if (device_index == WB_INVALID_INDEX)
                return false;
            AudioDeviceWASAPI& output_device = output_devices[device_index];
            if (!output.open(output_device.device))
                return false;
        }

        if (input_device_id != 0) {
            uint32_t device_index = find_device_index_(input_devices, input_device_id);
            if (device_index == WB_INVALID_INDEX) {
                output.close();
                return false;
            }
            AudioDeviceWASAPI& input_device = input_devices[device_index];
            if (!input.open(input_device.device)) {
                output.close();
                return false;
            }
        }

        min_period = std::max(output.absolute_min_period, input.absolute_min_period);
        buffer_alignment =
            std::max(output.low_latency_buffer_alignment, input.low_latency_buffer_alignment);

        // Check all possible exclusive mode formats
        for (auto smp_format : compatible_formats) {
            for (auto sample_rate : compatible_sample_rates) {
                for (auto channels : compatible_channel_count) {
                    uint32_t sample_rate_bit_mask = 1U << (uint32_t)sample_rate.second;
                    uint32_t format_bit_mask = 1U << (uint32_t)smp_format;
                    WAVEFORMATEXTENSIBLE format =
                        to_waveformatex(smp_format, sample_rate.first, channels);
                    bool output_format_supported = SUCCEEDED(output.client->IsFormatSupported(
                        AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));
                    bool input_format_supported = SUCCEEDED(input.client->IsFormatSupported(
                        AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&format, nullptr));

                    if (output_format_supported) {
                        exclusive_output_format_mask |= format_bit_mask;
                        exclusive_output_sample_rate_mask |= sample_rate_bit_mask;
                        exclusive_sample_rate_mask |= sample_rate_bit_mask;
                    }

                    if (input_format_supported) {
                        exclusive_input_format_mask |= format_bit_mask;
                        exclusive_input_sample_rate_mask |= sample_rate_bit_mask;
                        exclusive_sample_rate_mask |= sample_rate_bit_mask;
                    }
                }
            }
        }

        open = true;
        return true;
    }

    void close_device() override {
        if (!open)
            return;
        Log::info("Closing audio devices...");
        output.close();
        input.close();
        open = false;
        min_period = 0;
        buffer_alignment = 0;
    }

    bool start(bool exclusive_mode, AudioDevicePeriod buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate) override {

        return true;
    }

    void end() override {}

    bool scan_audio_endpoints(EDataFlow type, std::vector<AudioDeviceWASAPI>& endpoints) {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (FAILED(result))
            return false;

        ComPtr<IMMDeviceCollection> device_collection;
        enumerator->EnumAudioEndpoints(type, DEVICE_STATE_ACTIVE, &device_collection);

        uint32_t count = 0;
        device_collection->GetCount(&count);

        ComPtr<IMMDevice> default_device;
        wchar_t* default_device_id;
        enumerator->GetDefaultAudioEndpoint(type, eConsole, &default_device);
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
            IMMDevice* device;
            device_collection->Item(i, &device);

            ComPtr<IPropertyStore> property_store;
            PROPVARIANT var_name;
            PropVariantInit(&var_name);
            device->OpenPropertyStore(STGM_READ, &property_store);
            property_store->GetValue(PKEY_Device_FriendlyName, &var_name);

            wchar_t* device_id;
            device->GetId(&device_id);

            std::wstring_view device_str_id(device_id);
            AudioDeviceID id = std::hash<std::wstring_view> {}(device_str_id);
            AudioDeviceWASAPI& endpoint = endpoints.emplace_back();
            wcstombs_s(nullptr, endpoint.properties.name, var_name.pwszVal,
                       sizeof(endpoint.properties.name));
            endpoint.properties.id = id;
            endpoint.properties.type = device_type;
            endpoint.properties.io_type = AudioIOType::WASAPI;
            endpoint.device = device;

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

    uint32_t find_device_index_(const std::vector<AudioDeviceWASAPI>& devices, AudioDeviceID id) {
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
            return WB_INVALID_INDEX;
        return idx;
    }

    static void audio_thread_runner(AudioIOWASAPI* instance) {}
};

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

namespace wb {
AudioIO* create_audio_io_wasapi() {
    return nullptr;
}
} // namespace wb

#endif // WIN32