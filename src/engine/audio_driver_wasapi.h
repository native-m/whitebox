#pragma once

#include "audio_stream.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>

namespace wb
{
    struct AudioDeviceWASAPI
    {
        AudioDeviceProperties properties;
        IMMDevice* device;
    };

    struct AudioDriverWASAPI : public AudioDriver
    {
        std::vector<AudioDeviceWASAPI> output_devices;
        std::vector<AudioDeviceWASAPI> input_devices;
        IAudioClient* input_client = nullptr;
        IAudioClient* output_client = nullptr;
        REFERENCE_TIME input_min_period;
        REFERENCE_TIME output_min_period;
        IAudioRenderClient* render_client = nullptr;
        AudioMode input_mode{};
        AudioMode output_mode{};
        HANDLE input_buffer_event;
        HANDLE output_buffer_event;

        ~AudioDriverWASAPI();
        bool init_driver() override;
        std::vector<AudioDeviceProperties> get_input_devices() const override;
        std::vector<AudioDeviceProperties> get_output_devices() const override;
        bool open_devices(AudioDeviceID input_id, AudioDeviceID output_id) override;
        bool check_input_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const override;
        bool check_output_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const override;
        bool start_stream(bool exclusive, uint32_t buffer_size, const AudioMode& input_audio_mode, const AudioMode& output_audio_mode, Engine* engine) override;
        void close_devices() override;

        bool scan_audio_endpoints_(EDataFlow type, std::vector<AudioDeviceWASAPI>& endpoints);
        void release_all_devices_(std::vector<AudioDeviceWASAPI>& endpoints);
        bool check_audio_mode_(IAudioClient* audio_client, bool exclusive_mode, const AudioMode& mode) const;
        bool init_device_stream_(IAudioClient* audio_client, bool exclusive_mode, uint32_t buffer_size, const AudioMode& audio_mode);
        uint32_t get_sample_write_count_(IAudioClient* audio_client) const;
        void stop_audio_thread_();
        void audio_thread_runner_();
    };
}