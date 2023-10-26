#pragma once
#include "../core/audio_format.h"
#include "../core/audio_buffer.h"
#include "../stdpch.h"
#include "engine.h"

namespace wb
{
    struct AudioDevice;
    using AudioDeviceID = uint64_t;

    enum class AudioDriverType
    {
        WASAPI,
        DirectSound,
    };

    enum class AudioDeviceType
    {
        Input,
        Output,
    };

    struct AudioMode
    {
        AudioFormat format;
        uint16_t    channels;
        uint32_t    sample_rate;
    };
    using AudioModeString = std::vector<std::pair<AudioMode, char[128]>>;

    struct AudioDeviceProperties
    {
        char            name[128]{};
        AudioDeviceID   id;
        AudioDeviceType type;
        AudioDriverType driver_type;
    };

    using AudioProcessCallback = void(*)(void* userdata);
    using AudioEventCallback = void(*)(void* userdata);

    struct AudioIOManager
    {
        static std::vector<AudioDevice*> output_devices;
        static std::vector<AudioDevice*> input_devices;
        static void init();
        static void shutdown();
    };

    // Audio stream (AE) API
    bool ae_open_driver(AudioDriverType type);
    std::vector<AudioDeviceProperties> ae_get_input_devices();
    std::vector<AudioDeviceProperties> ae_get_output_devices();
    AudioDeviceProperties ae_get_default_input_device();
    AudioDeviceProperties ae_get_default_output_device();
    bool ae_open_devices(AudioDeviceID input_id, AudioDeviceID output_id);
    bool ae_is_device_open();
    bool ae_check_input_mode_support(bool exclusive_mode, const AudioMode& audio_mode);
    bool ae_check_output_mode_support(bool exclusive_mode, const AudioMode& audio_mode);
    bool ae_start_stream(bool exclusive, uint32_t buffer_size, const AudioMode& input_audio_mode, const AudioMode& output_audio_mode, Engine* engine);
    void ae_close_devices();
    void ae_close_driver();

    struct AudioDriver
    {
        AudioDeviceProperties default_output_device;
        AudioDeviceProperties default_input_device;
        bool open = false;
        bool exclusive_stream = false;
        uint32_t stream_buffer_size;
        uint32_t actual_buffer_size;
        std::thread audio_thread;
        std::atomic<bool> running;
        Engine* current_engine;

        virtual ~AudioDriver() { }
        virtual bool init_driver() = 0;
        virtual std::vector<AudioDeviceProperties> get_input_devices() const = 0;
        virtual std::vector<AudioDeviceProperties> get_output_devices() const = 0;
        virtual bool open_devices(AudioDeviceID input_id, AudioDeviceID output_id) = 0;
        virtual bool check_input_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const = 0;
        virtual bool check_output_mode_support(bool exclusive_mode, const AudioMode& audio_mode) const = 0;
        virtual bool start_stream(bool exclusive, uint32_t buffer_size, const AudioMode& input_audio_mode, const AudioMode& output_audio_mode, Engine* engine) = 0;
        virtual void close_devices() = 0;
    };
}