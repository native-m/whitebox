#include "audio_io.h"

#include <vector>
#include <iostream>
#include <cstring>

#ifdef WB_PLATFORM_LINUX

extern "C" {
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
}

#include "pulseaudio_devicelist_wrapper.h"

namespace wb {

struct AudioDevicePulseAudio {
    AudioDeviceProperties properties;
};

inline static pa_sample_format_t get_bit_sizes(AudioFormat audio_format) {
    switch (audio_format) {
        case AudioFormat::I8:
            return PA_SAMPLE_U8;
        case AudioFormat::I16:
            return PA_SAMPLE_S16LE;
        case AudioFormat::I24:
            return PA_SAMPLE_S24LE;
        case AudioFormat::I24_X8:
            return PA_SAMPLE_S24_32LE;
        case AudioFormat::I32:
            return PA_SAMPLE_S32LE;
        case AudioFormat::F32:
            return PA_SAMPLE_FLOAT32LE;
        default:
            break;
    }
    return {};
}

inline static pa_sample_spec get_sample_spec(AudioFormat sample_format, uint32_t sample_rate,
                                             uint16_t channels) {
    pa_sample_spec sample_spec;

    pa_sample_format_t bit_size = get_bit_sizes(sample_format);

    sample_spec.format = bit_size;
    sample_spec.rate = sample_rate;
    sample_spec.channels = channels;

    return sample_spec;
}

struct AudioIOPulseAudio : public AudioIO {
    std::vector<AudioDevicePulseAudio> input_devices;
    std::vector<AudioDevicePulseAudio> output_devices;

    pa_mainloop* mainloop = nullptr;
    pa_mainloop_api* mainloop_api = nullptr;
    pa_context* context = nullptr;
    pa_stream* stream = nullptr;
    pa_sample_spec sample_spec;
    pa_device selected_output_device;
    pa_device selected_input_device;

    bool running = false;

    PulseAudioDeviceList device_list;

    AudioIOPulseAudio() {
        // do nothing
    }

    ~AudioIOPulseAudio() {
        // do nothing
    }

    bool init() {
        mainloop = pa_mainloop_new();
        mainloop_api = pa_mainloop_get_api(mainloop);
        context = pa_context_new(mainloop_api, "audio-device-manager");

        pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);

        device_list.set_context(context);

        return rescan_devices();
    }

    bool rescan_devices() override {
        auto devices = device_list.update_device_lists(mainloop, mainloop_api);

        if (!devices) {
            return false;
        }

        input_devices.clear();
        output_devices.clear();

        auto input_device_list = device_list.get_record_device_list();
        for (const auto& device : input_device_list) {
            AudioDevicePulseAudio audio_device;

            std::string_view str_id = device.id;

            AudioDeviceID id = std::hash<std::string_view> {}(str_id);

            audio_device.properties.id = id;
            std::strcpy(audio_device.properties.name, device.name.c_str());
            audio_device.properties.type = AudioDeviceType::Input;
            audio_device.properties.io_type = AudioIOType::PulseAudio;

            input_devices.push_back(audio_device);
        }

        auto output_device_list = device_list.get_sink_device_list();

        for (const auto& device : output_device_list) {
            AudioDevicePulseAudio audio_device;

            std::string_view str_id = device.id;

            AudioDeviceID id = std::hash<std::string_view> {}(str_id);

            audio_device.properties.id = id;
            std::strcpy(audio_device.properties.name, device.name.c_str());
            audio_device.properties.type = AudioDeviceType::Output;
            audio_device.properties.io_type = AudioIOType::PulseAudio;

            output_devices.push_back(audio_device);
        }

        input_device_count = input_devices.size();
        output_device_count = output_devices.size();

        return true;
    }

    uint32_t get_input_device_count() const {
        return input_device_count;
    }
    uint32_t get_output_device_count() const {
        return output_device_count;
    }

    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return input_devices[idx].properties;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return output_devices[idx].properties;
    }

    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_id) override {
        if (!context || !stream) {
            std::cerr << "Context or stream is not initialized." << std::endl;
            return false;
        }

        // Get the sink and source devices by their IDs
        auto output_device = device_list.get_sink_device_by_index(output_device_id);
        auto input_device = device_list.get_sink_device_by_index(input_device_id);

        if (!output_device.has_value() || !input_device.has_value()) {
            std::cerr << "Invalid output or input device ID." << std::endl;
            return false;
        }

        selected_output_device = output_device.value();
        selected_input_device = input_device.value();

        return true;
    }

    void close_device() override {
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
    }

    bool start(Engine* engine, bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate,
               AudioThreadPriority priority) override {

        if (running) {
            return false;
        }

        if (!context || !stream) {
            std::cerr << "Context or stream is not initialized." << std::endl;
            return false;
        }

        uint32_t sample_rate_value = get_sample_rate_value(sample_rate);

        AudioDevicePeriod period = buffer_size_to_period(buffer_size, sample_rate_value);

        sample_spec = get_sample_spec(output_format, sample_rate_value, 2);

        pa_buffer_attr buffer_attr;

        buffer_attr.maxlength = (uint32_t)-1;

        buffer_attr.tlength = buffer_size;

        buffer_attr.prebuf = (uint32_t)-1;

        buffer_attr.minreq = (uint32_t)-1;

        pa_stream_flags_t flags = PA_STREAM_START_CORKED;

        stream = pa_stream_new(context, "audio-stream", &sample_spec, nullptr);

        if (!stream) {
            std::cerr << "Failed to create stream." << std::endl;
            return false;
        }

        pa_stream_connect_playback(stream, selected_output_device.id.c_str(), nullptr,
                                   PA_STREAM_NOFLAGS, nullptr, nullptr);

        return true;
    }

    void stop() override {
        // do nothing
    }
};

AudioIO* create_audio_io_pulseaudio() {
    AudioIOPulseAudio* audio_io = new AudioIOPulseAudio();

    if (!audio_io->init()) {
        delete audio_io;
        return nullptr;
    }

    return audio_io;
}
}

#else

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#endif