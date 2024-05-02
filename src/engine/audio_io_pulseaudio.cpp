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
    pa_context *context = nullptr;
    pa_stream *stream = nullptr;
    pa_sample_spec sample_spec;

    PulseAudioDeviceList device_list;

    ~AudioIOPulseAudio() {
        // do nothing
    }

    bool init() {
        mainloop = pa_mainloop_new();
        mainloop_api = pa_mainloop_get_api(mainloop);
        context = pa_context_new(mainloop_api, "audio-device-manager");

        pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);

        pa_mainloop_run(mainloop, NULL);

        sample_spec = get_sample_spec(shared_mode_output_format, static_cast<uint32_t>(shared_mode_sample_rate), 2);

        stream = pa_stream_new(context, "audio-stream", &sample_spec, NULL);

        if (!stream) {
            std::cerr << "pa_stream_new failed" << std::endl;
            return false;
        }

        return rescan_devices();
    }

    bool rescan_devices() override {
        auto devices = device_list.update_device_lists(mainloop, mainloop_api, context);

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

        return true;
    }
    
    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return input_devices[idx].properties;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return output_devices[idx].properties;
    }

    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) override {
        auto output_device = device_list.get_sink_device_by_index(output_device_id);
        auto input_device = device_list.get_record_device_by_index(input_device_idx);

        if (pa_stream_connect_playback(stream, output_device.id.c_str(), NULL, PA_STREAM_NOFLAGS, NULL, NULL) != 0) {
            std::cerr << "pa_stream_connect_playback failed" << std::endl;
            return false;
        }

        if (pa_stream_connect_record(stream, input_device.id.c_str(), NULL, PA_STREAM_NOFLAGS) != 0) {
            std::cerr << "pa_stream_connect_record failed" << std::endl;
            return false;
        }

        return true;
    }

    void close_device() override {
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
    }

    bool start(bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate,
               AudioThreadPriority priority) override {
        return false;
    }

    void stop() override {
        // do nothing
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
