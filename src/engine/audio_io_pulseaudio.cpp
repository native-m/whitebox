#include "audio_io.h"

#ifdef WB_PLATFORM_LINUX

extern "C" {
#include <pulse/error.h>
#include <pulse/simple.h>
}

namespace wb {

struct AudioIOPulseAudio : public AudioIO {
    pa_simple* pa_connection = nullptr;

    ~AudioIOPulseAudio() {
        // do nothing
    }

    bool init() { return false; }

    bool rescan_devices() override { return false; }

    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return default_input_device;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return default_output_device;
    }

    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) override {

        pa_connection = nullptr;

        return false;
    }

    void close_device() override {
        // do nothing
    }

    bool start(Engine* engine, bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate,
               AudioThreadPriority priority) override {
        return false;
    }
};

AudioIO* create_audio_io_pulseaudio() {
    AudioIOPulseAudio* audio_io = new (std::nothrow) AudioIOPulseAudio();
    if (!audio_io)
        return nullptr;
    if (!audio_io->init())
        return nullptr;
    return audio_io;
}
} // namespace wb

#else

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#endif