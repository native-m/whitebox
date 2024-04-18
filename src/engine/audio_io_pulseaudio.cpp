#include "audio_io.h"

#ifdef __linux__

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#else

extern "C" {
#include <pulse/simple.h>
#include <pulse/error.h>
}

namespace wb {

struct AudioIOPulseAudio : public AudioIO {

    pa_simple *pa_connection = nullptr;

    bool rescan_devices() override {
        return false;
    }
    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return default_input_device;
    };
    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return default_output_device;
    };
    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) override {

        pa_connection = nullptr;

        return false;
    };
    void close_device() override {
        // do nothing
    };
    bool start(bool exclusive_mode, AudioDevicePeriod period, AudioFormat input_format,
                       AudioFormat output_format, AudioDeviceSampleRate sample_rate) override {
        return false;
    };
    void end() override {
        // do nothing
    };

    ~AudioIOPulseAudio() {
        // do nothing
    };
};

AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#endif