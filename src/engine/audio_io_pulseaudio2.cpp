#include "audio_io.h"

#ifdef WB_PLATFORM_LINUX
#include "core/debug.h"
#include "core/defer.h"
#include "core/vector.h"
#include <pulse/pulseaudio.h>
#include <pulse/rtclock.h>

namespace wb {

struct AudioDevicePulseAudio2 {
    AudioDeviceProperties properties;
    int index;
    std::string hw_name;
};

struct AudioIOPulseAudio2 : public AudioIO {
    pa_mainloop* main_loop_ {};
    pa_mainloop_api* ml_api_ {};
    pa_context* context_ {};
    pa_context_state_t ctx_state_ {};

    Vector<AudioDevicePulseAudio2> output_devices;
    Vector<AudioDevicePulseAudio2> input_devices;

    AudioDeviceProperties dummy_input_device {
        .name = "Dummy Audio",
        .id = 0,
        .type = AudioDeviceType::Input,
        .io_type = AudioIOType::PulseAudio,
    };

    AudioDeviceProperties dummy_output_device {
        .name = "Dummy Audio",
        .id = 0,
        .type = AudioDeviceType::Output,
        .io_type = AudioIOType::PulseAudio,
    };

    virtual ~AudioIOPulseAudio2() {
        if (context_) {
            pa_context_disconnect(context_);
            pa_context_unref(context_);
        }
        if (main_loop_) {
            pa_mainloop_free(main_loop_);
        }
    }

    bool init() {
        main_loop_ = pa_mainloop_new();
        if (!main_loop_) {
            return false;
        }

        ml_api_ = pa_mainloop_get_api(main_loop_);
        context_ = pa_context_new(ml_api_, "wb_pulseaudio");
        if (!context_) {
            return false;
        }

        pa_context_set_state_callback(context_, &state_callback, this);
        pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
        if (!wait_for_context()) {
            return false;
        }

        return rescan_devices();
    }

    bool exclusive_mode_support() override { return false; }
    bool shared_mode_support() override { return true; }

    /*
        Rescan available device that can be used by whitebox.
    */
    bool rescan_devices() override {
        auto sink_info_cb = [](pa_context* c, const pa_sink_info* i, int eol, void* userdata) {
            if (eol > 0) {
                return;
            }
            AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
            AudioDevicePulseAudio2& device = current->output_devices.emplace_back();
            std::string_view description(i->description);
            std::strncpy(device.properties.name, i->description,
                         sizeof(AudioDeviceProperties::name));
            device.properties.id = std::hash<std::string_view> {}(description);
            device.properties.io_type = AudioIOType::PulseAudio;
            device.properties.type = AudioDeviceType::Output;
            device.index = i->index;
            device.hw_name = i->name;
        };

        auto source_info_cb = [](pa_context* c, const pa_source_info* i, int eol, void* userdata) {
            if (eol > 0) {
                return;
            }
            AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
            AudioDevicePulseAudio2& device = current->input_devices.emplace_back();
            std::string_view description(i->description);
            std::strncpy(device.properties.name, i->description,
                         sizeof(AudioDeviceProperties::name));
            device.properties.id = std::hash<std::string_view> {}(description);
            device.properties.io_type = AudioIOType::PulseAudio;
            device.properties.type = AudioDeviceType::Input;
            device.index = i->index;
            device.hw_name = i->name;
        };

        pa_operation* op = pa_context_get_sink_info_list(context_, sink_info_cb, this);
        if (!wait_for_context(op)) {
            return false;
        }

        op = pa_context_get_source_info_list(context_, source_info_cb, this);
        if (!wait_for_context(op)) {
            return false;
        }

        for (const auto& output : output_devices) {
            Log::debug("Found output device: {}", output.properties.name);
        }

        for (const auto& input : input_devices) {
            Log::debug("Found input device: {}", input.properties.name);
        }

        return true;
    }

    virtual uint32_t get_input_device_index(AudioDeviceID id) const {
        return find_device_index(input_devices, id);
    }

    virtual uint32_t get_output_device_index(AudioDeviceID id) const {
        return find_device_index(output_devices, id);
    }

    virtual const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const {
        return input_devices[idx].properties;
    }

    virtual const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const {
        return output_devices[idx].properties;
    }

    /*
        Open input and output devices to ensure they are ready for use.
        Usually, the implementation gets the hardware information in here.
    */
    virtual bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) {
        return false;
    }

    /*
        Closes input and output devices after being used by the application.
    */
    virtual void close_device() {}

    /*
        Starts the audio engine.
        Audio thread will be launched here.
    */
    virtual bool start(Engine* engine, bool exclusive_mode, uint32_t buffer_size,
                       AudioFormat input_format, AudioFormat output_format,
                       AudioDeviceSampleRate sample_rate, AudioThreadPriority priority) {
        return false;
    }

    bool wait_for_context(pa_operation* operation = nullptr) {
        while (true) {
            pa_mainloop_iterate(main_loop_, 1, nullptr);
            if (operation) {
                pa_operation_state_t op_state = pa_operation_get_state(operation);
                if (op_state == PA_OPERATION_DONE) {
                    pa_operation_unref(operation);
                    break;
                } else if (op_state == PA_OPERATION_CANCELLED) {
                    pa_operation_unref(operation);
                    return false;
                }
            } else {
                if (ctx_state_ == PA_CONTEXT_READY) {
                    break;
                }
                if (ctx_state_ == PA_CONTEXT_FAILED || ctx_state_ == PA_CONTEXT_TERMINATED) {
                    return false;
                }
            }
        }
        return true;
    }

    uint32_t find_device_index(const Vector<AudioDevicePulseAudio2>& devices,
                               AudioDeviceID id) const {
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

    static void state_callback(pa_context* ctx, void* userdata) {
        AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
        current->ctx_state_ = pa_context_get_state(ctx);
    }
};

AudioIO* create_audio_io_pulseaudio2() {
    AudioIOPulseAudio2* audio_io = new AudioIOPulseAudio2();

    if (!audio_io->init()) {
        delete audio_io;
        return nullptr;
    }

    return audio_io;
}

} // namespace wb
#else
namespace wb {
AudioIO* create_audio_io_wasapi() {
    return nullptr;
}
} // namespace wb
#endif