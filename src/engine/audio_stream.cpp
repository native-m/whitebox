#include "audio_stream.h"
#include "audio_driver_wasapi.h"
#include "../types.h"
#include "../core/debug.h"

namespace wb
{
    AudioDriver* current_driver = nullptr;

    bool ae_open_driver(AudioDriverType type)
    {
        if (current_driver) return false;
        
        switch (type) {
            case AudioDriverType::WASAPI:
            {
                current_driver = new AudioDriverWASAPI();
                break;
            }
        }

        return current_driver->init_driver();
    }

    std::vector<AudioDeviceProperties> ae_get_input_devices()
    {
        return current_driver->get_input_devices();
    }

    std::vector<AudioDeviceProperties> ae_get_output_devices()
    {
        return current_driver->get_output_devices();
    }

    AudioDeviceProperties ae_get_default_input_device()
    {
        return current_driver->default_input_device;
    }

    AudioDeviceProperties ae_get_default_output_device()
    {
        return current_driver->default_output_device;
    }

    bool ae_open_devices(AudioDeviceID input_id, AudioDeviceID output_id)
    {
        return current_driver->open_devices(input_id, output_id);
    }

    bool ae_is_device_open()
    {
        return current_driver->open;
    }

    bool ae_check_input_mode_support(bool exclusive_mode, const AudioMode& audio_mode)
    {
        return current_driver->check_input_mode_support(exclusive_mode, audio_mode);
    }

    bool ae_check_output_mode_support(bool exclusive_mode, const AudioMode& audio_mode)
    {
        return current_driver->check_output_mode_support(exclusive_mode, audio_mode);
    }

    bool ae_start_stream(bool exclusive, uint32_t buffer_size, const AudioMode& input_audio_mode, const AudioMode& output_audio_mode, Engine* engine)
    {
        // Output stream must present
        if (output_audio_mode.format == AudioFormat::Unknown) return false;
        return current_driver->start_stream(exclusive, buffer_size, input_audio_mode, output_audio_mode, engine);
    }

    void ae_close_devices()
    {
        current_driver->close_devices();
    }

    void ae_close_driver()
    {
        delete current_driver;
        current_driver = nullptr;
    }
}