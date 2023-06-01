#include "audio_driver_wasapi.h"
#include "../core/debug.h"
#include <ranges>
#include <chrono>
#include <cmath>
#include <Functiondiscoverykeys_devpkey.h>

using namespace std::chrono_literals;

namespace wb
{
    AudioDeviceWASAPI* find_endpoint_by_id(std::vector<AudioDeviceWASAPI>& endpoints, AudioDeviceID id)
    {
        auto endpoint = endpoints
            | std::views::filter([id](const AudioDeviceWASAPI& endpoint) { return endpoint.properties.id == id; })
            | std::views::take(1);
        return &endpoint.front();
    }

    inline static std::pair<WORD, WORD> mmformat_from_audio_format(AudioFormat audio_format)
    {
        switch (audio_format) {
            case AudioFormat::I8:   return { WAVE_FORMAT_PCM, 8 };
            case AudioFormat::I16:  return { WAVE_FORMAT_PCM, 16 };
            case AudioFormat::I24:  return { WAVE_FORMAT_PCM, 24 };
            case AudioFormat::I32:  return { WAVE_FORMAT_PCM, 32 };
            case AudioFormat::F32:  return { WAVE_FORMAT_IEEE_FLOAT, 32 };
            default:                break;
        }
        return {};
    }

    inline static WAVEFORMATEX to_waveformatex(const AudioMode& mode)
    {
        auto [format, bits] = mmformat_from_audio_format(mode.format);
        return WAVEFORMATEX{
            .wFormatTag = format,
            .nChannels = mode.channels,
            .nSamplesPerSec = mode.sample_rate,
            .nAvgBytesPerSec = mode.channels * mode.sample_rate * (bits / 8),
            .nBlockAlign = (WORD)(((int32_t)mode.channels * bits) / 8),
            .wBitsPerSample = bits,
        };
    }

    inline static uint32_t to_sample_count(REFERENCE_TIME ref_time, uint32_t sample_rate)
    {
        constexpr double sec_in_100_ns_units = 10000000.0;
        return (uint32_t)(sample_rate * ref_time / sec_in_100_ns_units);
    }

    inline static REFERENCE_TIME to_reference_time(uint32_t sample_count, uint32_t sample_rate)
    {
        constexpr double sec_in_100_ns_units = 10000000.0;
        return (uint32_t)(sec_in_100_ns_units * (sample_count / (double)sample_rate));
    }

    AudioDriverWASAPI::~AudioDriverWASAPI()
    {
        close_devices();
        release_all_devices_(output_devices);
        release_all_devices_(input_devices);
    }

    bool AudioDriverWASAPI::init_driver()
    {
        if (!scan_audio_endpoints_(eCapture, input_devices))
            return false;
        if (!scan_audio_endpoints_(eRender, output_devices))
            return false;

        return true;
    }
    
    std::vector<AudioDeviceProperties> AudioDriverWASAPI::get_input_devices() const
    {
        auto view = input_devices | std::views::transform([](const AudioDeviceWASAPI& endpoint) { return endpoint.properties; });
        return std::vector<AudioDeviceProperties>(view.begin(), view.end());
    }
    
    std::vector<AudioDeviceProperties> AudioDriverWASAPI::get_output_devices() const
    {
        auto view = output_devices | std::views::transform([](const AudioDeviceWASAPI& endpoint) { return endpoint.properties; });
        return std::vector<AudioDeviceProperties>(view.begin(), view.end());
    }
    
    bool AudioDriverWASAPI::open_devices(AudioDeviceID input_id, AudioDeviceID output_id)
    {
        auto input_endpoint = find_endpoint_by_id(input_devices, input_id);
        auto output_endpoint = find_endpoint_by_id(output_devices, output_id);
        
        if (FAILED(input_endpoint->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&input_client))) {
            return false;
        }

        if (FAILED(output_endpoint->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&output_client))) {
            return false;
        }

        input_client->GetDevicePeriod(&input_min_period, nullptr);
        output_client->GetDevicePeriod(&output_min_period, nullptr);

        open = true;
        return true;
    }

    bool AudioDriverWASAPI::check_input_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const
    {
        return check_audio_mode_(input_client, exclusive_mode, audio_mode);
    }

    bool AudioDriverWASAPI::check_output_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const
    {
        return check_audio_mode_(output_client, exclusive_mode, audio_mode);
    }
    
    bool AudioDriverWASAPI::start_stream(bool exclusive, uint32_t buffer_size, const AudioMode& input_audio_mode, const AudioMode& output_audio_mode, Engine* engine)
    {
        if (running) return false;

        uint32_t min_buffer_size = std::max(to_sample_count(input_min_period, input_audio_mode.sample_rate),
                                            to_sample_count(output_min_period, output_audio_mode.sample_rate));
        uint32_t clamped_buffer_size = std::max(min_buffer_size, buffer_size);

        if (!init_device_stream_(output_client, exclusive, clamped_buffer_size, output_audio_mode)) return false;
        if (input_audio_mode.format != AudioFormat::Unknown)
            if (!init_device_stream_(input_client, exclusive, clamped_buffer_size, input_audio_mode))
                return false;
        
        input_buffer_event = CreateEvent(nullptr, FALSE, FALSE, "WhiteBoxInputEv");
        if (!input_buffer_event)
            return false;

        output_buffer_event = CreateEvent(nullptr, FALSE, FALSE, "WhiteBoxOutputEv");
        if (!output_buffer_event) {
            CloseHandle(input_buffer_event);
            return false;
        }

        output_client->SetEventHandle(output_buffer_event);
        output_client->GetBufferSize(&actual_buffer_size);
        output_client->GetService(IID_PPV_ARGS(&render_client));

        /*REFERENCE_TIME stream_latency;
        HRESULT hr = output_client->GetStreamLatency(&stream_latency);
        Log::info("{}", to_sample_count(stream_latency, output_audio_mode.sample_rate));*/

        exclusive_stream = exclusive;
        stream_buffer_size = clamped_buffer_size;
        input_mode = input_audio_mode;
        output_mode = output_audio_mode;
        current_engine = engine;
        audio_thread = std::thread(&AudioDriverWASAPI::audio_thread_runner_, this);
        running = true;
        return true;
    }

    void AudioDriverWASAPI::close_devices()
    {
        if (!open) return;
        if (running) {
            stop_audio_thread_();
            render_client->Release();
            CloseHandle(input_buffer_event);
            CloseHandle(output_buffer_event);
            current_engine = nullptr;
            input_mode = {};
            output_mode = {};
            stream_buffer_size = 0;
            exclusive_stream = false;
        }
        input_client->Release();
        output_client->Release();
    }
    
    bool AudioDriverWASAPI::scan_audio_endpoints_(EDataFlow type, std::vector<AudioDeviceWASAPI>& endpoints)
    {
        IMMDeviceEnumerator* device_enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&device_enumerator))) {
            WB_ASSERT(false && "Failed to initialize audio I/O");
        }

        IMMDeviceCollection* device_collection;
        device_enumerator->EnumAudioEndpoints(type, DEVICE_STATE_ACTIVE, &device_collection);

        uint32_t count = 0;
        device_collection->GetCount(&count);

        IMMDevice* default_device;
        wchar_t* default_device_id;
        device_enumerator->GetDefaultAudioEndpoint(type, eConsole, &default_device);
        default_device->GetId(&default_device_id);

        std::wstring_view default_device_str_id(default_device_id);

        for (uint32_t i = 0; i < count; i++) {
            IMMDevice* device;
            device_collection->Item(i, &device);

            IPropertyStore* property_store;
            PROPVARIANT var_name;
            PropVariantInit(&var_name);
            device->OpenPropertyStore(STGM_READ, &property_store);
            property_store->GetValue(PKEY_Device_FriendlyName, &var_name);

            wchar_t* device_id;
            device->GetId(&device_id);
            WB_ASSERT(device_id != nullptr);

            std::wstring_view device_str_id(device_id);
            AudioDeviceWASAPI& endpoint = endpoints.emplace_back();
            wcstombs_s(nullptr, endpoint.properties.name, var_name.pwszVal, 128);
            endpoint.properties.id = std::hash<std::wstring_view>{}(std::wstring_view(device_id));
            endpoint.properties.type = AudioDeviceType::Output;
            endpoint.properties.driver_type = AudioDriverType::WASAPI;
            endpoint.device = device;

            if (device_str_id == default_device_str_id) {
                switch (type) {
                    case eRender:
                        default_output_device = endpoint.properties;
                        break;
                    case eCapture:
                        default_input_device = endpoint.properties;
                        break;
                }
            }

            CoTaskMemFree(device_id);
            PropVariantClear(&var_name);
        }

        default_device->Release();
        device_collection->Release();
        device_enumerator->Release();

        return true;
    }

    void AudioDriverWASAPI::release_all_devices_(std::vector<AudioDeviceWASAPI>& endpoints)
    {
        for (auto& endpoint : endpoints)
            endpoint.device->Release();
    }

    bool AudioDriverWASAPI::check_audio_mode_(IAudioClient* audio_client, bool exclusive_mode, const AudioMode& mode) const
    {
        if (audio_client == nullptr)
            return false;

        if (!(is_integer_format(mode.format) || is_floating_point_format(mode.format)))
            return false;

        if (exclusive_mode) {
            WAVEFORMATEX waveformat = to_waveformatex(mode);
            return SUCCEEDED(audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &waveformat, nullptr));
        }

        WAVEFORMATEXTENSIBLE* shared_mode_format;
        if (FAILED(audio_client->GetMixFormat((WAVEFORMATEX**)&shared_mode_format)))
            WB_ASSERT(false && "Failed to open audio output device");

        if (shared_mode_format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            if (shared_mode_format->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && !is_integer_format(mode.format))
                return false;
            else if (shared_mode_format->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && !is_floating_point_format(mode.format))
                return false;
        }
        else if (shared_mode_format->Format.wFormatTag == WAVE_FORMAT_PCM && !is_integer_format(mode.format))
            return false;
        else if (shared_mode_format->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && !is_floating_point_format(mode.format))
            return false;

        shared_mode_format->Format.nChannels = mode.channels;
        shared_mode_format->Format.nSamplesPerSec = mode.sample_rate;
        shared_mode_format->Format.nAvgBytesPerSec = mode.channels * mode.sample_rate * (shared_mode_format->Format.wBitsPerSample / 8);

        WAVEFORMATEXTENSIBLE* closest_format;
        bool supported = SUCCEEDED(audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                                   (const WAVEFORMATEX*)shared_mode_format,
                                                                   (WAVEFORMATEX**)&closest_format));
        if (!supported) {
            CoTaskMemFree(shared_mode_format);
            return false;
        }

        CoTaskMemFree(shared_mode_format);
        CoTaskMemFree(closest_format);

        return supported;
    }

    // TODO: Use IAudioClient3 to get pure low-latency audio in shared mode
    bool AudioDriverWASAPI::init_device_stream_(IAudioClient* audio_client, bool exclusive_mode, uint32_t buffer_size, const AudioMode& audio_mode)
    {
        DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        WAVEFORMATEX waveformat = to_waveformatex(audio_mode);
        AUDCLNT_SHAREMODE share_mode;

        if (!exclusive_mode) {
            WAVEFORMATEX* mix_format;
            audio_client->GetMixFormat(&mix_format);
            if (mix_format->nSamplesPerSec != audio_mode.sample_rate)
                stream_flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            share_mode = AUDCLNT_SHAREMODE_SHARED;
            CoTaskMemFree(mix_format);
        }
        else {
            share_mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        }

        REFERENCE_TIME buffer_duration = to_reference_time(buffer_size, audio_mode.sample_rate);
        HRESULT result = audio_client->Initialize(share_mode, stream_flags, buffer_duration, buffer_duration, &waveformat, nullptr);
        uint32_t test = AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;
        if (FAILED(result))
            return false;

        return true;
    }

    uint32_t AudioDriverWASAPI::get_sample_write_count_(IAudioClient* audio_client) const
    {
        if (!exclusive_stream) {
            uint32_t padding;
            audio_client->GetCurrentPadding(&padding);
            return stream_buffer_size - padding;
        }
        return stream_buffer_size;
    }

    void AudioDriverWASAPI::stop_audio_thread_()
    {
        if (!running) return;
        running = false;
        audio_thread.join();
    }

    void AudioDriverWASAPI::audio_thread_runner_()
    {
        WB_ASSERT(is_floating_point_format(output_mode.format));
        AudioBuffer<float> buffer(output_mode.format, stream_buffer_size, output_mode.channels);
        float* l_buffer = buffer.get_write_pointer(0);
        float* r_buffer = buffer.get_write_pointer(1);
        float* audio_buffer;

        uint32_t output_write_count = get_sample_write_count_(output_client);
        render_client->GetBuffer(output_write_count, (BYTE**)&audio_buffer);
        render_client->ReleaseBuffer(output_write_count, AUDCLNT_BUFFERFLAGS_SILENT);

        output_client->Start();
        while (running.load(std::memory_order_relaxed)) {
            current_engine->process(buffer, output_mode.sample_rate);

            uint32_t samples = 0;
            while (samples < buffer.n_samples) {
                WaitForSingleObject(output_buffer_event, INFINITE);

                uint32_t num_frames_available = get_sample_write_count_(output_client);
                if (num_frames_available > (buffer.n_samples - samples))
                    num_frames_available = buffer.n_samples - samples;
                
                render_client->GetBuffer(num_frames_available, (BYTE**)&audio_buffer);
                for (uint32_t i = 0; i < num_frames_available; i++) {
                    audio_buffer[i * 2 + 0] = l_buffer[i + samples];
                    audio_buffer[i * 2 + 1] = r_buffer[i + samples];
                }
                render_client->ReleaseBuffer(num_frames_available, 0);
                
                samples += num_frames_available;
            }
        }
        WaitForSingleObject(output_buffer_event, INFINITE);
        output_client->Stop();
    }
}