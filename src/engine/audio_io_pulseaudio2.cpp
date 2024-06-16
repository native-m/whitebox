#include "audio_io.h"

#ifdef WB_PLATFORM_LINUX
#include "core/audio_buffer.h"
#include "core/audio_format_conv.h"
#include "core/debug.h"
#include "core/defer.h"
#include "core/vector.h"
#include "engine.h"
#include <pulse/pulseaudio.h>
#include <pulse/rtclock.h>

namespace wb {

inline AudioFormat to_audio_format(pa_sample_format format) {
    switch (format) {
        case PA_SAMPLE_S16LE:
            return AudioFormat::I16;
        case PA_SAMPLE_S24LE:
            return AudioFormat::I24;
        case PA_SAMPLE_S24_32LE:
            return AudioFormat::I24_X8;
        case PA_SAMPLE_S32LE:
            return AudioFormat::I32;
        case PA_SAMPLE_FLOAT32LE:
            return AudioFormat::F32;
        default:
            break;
    }

    return AudioFormat::Unknown;
}

inline pa_sample_spec to_sample_spec(AudioFormat format, AudioDeviceSampleRate sample_rate,
                                     uint16_t channels) {
    pa_sample_spec ret {
        .rate = get_sample_rate_value(sample_rate),
        .channels = (uint8_t)channels,
    };

    switch (format) {
        case AudioFormat::I16:
            ret.format = PA_SAMPLE_S16LE;
            break;
        case AudioFormat::I24:
            ret.format = PA_SAMPLE_S24LE;
            break;
        case AudioFormat::I24_X8:
            ret.format = PA_SAMPLE_S24_32LE;
            break;
        case AudioFormat::I32:
            ret.format = PA_SAMPLE_S32LE;
            break;
        case AudioFormat::F32:
            ret.format = PA_SAMPLE_FLOAT32LE;
            break;
        default:
            ret.format = PA_SAMPLE_INVALID;
            break;
    }

    return ret;
}

struct AudioDevicePulseAudio2 {
    AudioDeviceProperties properties;
    int index;
    std::string hw_name;
    pa_sample_spec default_sample_spec;
    pa_usec_t latency;
    pa_usec_t configured_latency;
};

struct AudioIOPulseAudio2 : public AudioIO {
    pa_mainloop* main_loop_ {};
    pa_mainloop_api* ml_api_ {};
    pa_context* context_ {};
    pa_context_state_t ctx_state_ {};
    AudioDevicePulseAudio2 output_ {};
    AudioDevicePulseAudio2 input_ {};
    pa_sample_spec output_sample_spec_ {};
    AudioFormat output_sample_format_ {};
    pa_stream* output_stream_ {};
    std::thread audio_thread_;
    Engine* engine_ {};
    std::atomic<bool> running_ = false;

    AudioBuffer<float> output_buffer_;
    void* conversion_buffer_;

    Vector<AudioDevicePulseAudio2> output_devices;
    Vector<AudioDevicePulseAudio2> input_devices;

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
            std::string_view name(i->name);
            std::strncpy(device.properties.name, i->description,
                         sizeof(AudioDeviceProperties::name));
            device.properties.id = std::hash<std::string_view> {}(name);
            device.properties.io_type = AudioIOType::PulseAudio;
            device.properties.type = AudioDeviceType::Output;
            device.index = i->index;
            device.hw_name = i->name;
            device.default_sample_spec = i->sample_spec;
            device.latency = i->latency;
            device.configured_latency = i->configured_latency;
        };

        auto source_info_cb = [](pa_context* c, const pa_source_info* i, int eol, void* userdata) {
            if (eol > 0) {
                return;
            }
            AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
            AudioDevicePulseAudio2& device = current->input_devices.emplace_back();
            std::string_view name(i->name);
            std::strncpy(device.properties.name, i->description,
                         sizeof(AudioDeviceProperties::name));
            device.properties.id = std::hash<std::string_view> {}(name);
            device.properties.io_type = AudioIOType::PulseAudio;
            device.properties.type = AudioDeviceType::Input;
            device.index = i->index;
            device.hw_name = i->name;
            device.default_sample_spec = i->sample_spec;
            device.latency = i->latency;
            device.configured_latency = i->configured_latency;
        };

        auto default_sink_info_cb = [](pa_context* c, const pa_sink_info* i, int eol,
                                       void* userdata) {
            if (eol > 0) {
                return;
            }
            if (i->index == 0) {
                return;
            }
            AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
            current->default_output_device = current->output_devices[i->index - 1].properties;
        };

        auto default_source_info_cb = [](pa_context* c, const pa_source_info* i, int eol,
                                         void* userdata) {
            if (eol > 0) {
                return;
            }
            if (i->index == 0) {
                return;
            }
            AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
            current->default_input_device = current->input_devices[i->index - 1].properties;
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
            Log::debug("Found output device ({}): {}", output.index, output.properties.name);
        }

        for (const auto& input : input_devices) {
            Log::debug("Found input device ({}): {}", input.index, input.properties.name);
        }

        op = pa_context_get_sink_info_by_name(context_, nullptr, default_sink_info_cb, this);
        if (!wait_for_context(op)) {
            return false;
        }

        op = pa_context_get_source_info_by_name(context_, nullptr, default_source_info_cb, this);
        if (!wait_for_context(op)) {
            return false;
        }

        input_device_count = (size_t)input_devices.size();
        output_device_count = (size_t)output_devices.size();

        return true;
    }

    uint32_t get_input_device_index(AudioDeviceID id) const override {
        return find_device_index(input_devices, id);
    }

    uint32_t get_output_device_index(AudioDeviceID id) const override {
        return find_device_index(output_devices, id);
    }

    const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const override {
        return input_devices[idx].properties;
    }

    const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const override {
        return output_devices[idx].properties;
    }

    /*
        Open input and output devices to ensure they are ready for use.
        Usually, the implementation gets the hardware information in here.
    */
    bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_id) override {
        Log::info("Opening audio devices...");

        if (output_device_id != 0) {
            uint32_t device_index = find_device_index(output_devices, output_device_id);
            if (device_index == WB_INVALID_AUDIO_DEVICE_INDEX) {
                return false;
            }
            AudioDevicePulseAudio2& output_device = output_devices[device_index];
            output_ = output_device;
        }

        if (input_device_id != 0) {
            uint32_t device_index = find_device_index(input_devices, input_device_id);
            if (device_index == WB_INVALID_AUDIO_DEVICE_INDEX) {
                return false;
            }
            AudioDevicePulseAudio2& input_device = input_devices[device_index];
            input_ = input_device;
        }

        min_period = buffer_size_to_period(128, 48000);
        buffer_alignment = 32;
        shared_mode_output_format = to_audio_format(output_.default_sample_spec.format);
        shared_mode_input_format = to_audio_format(input_.default_sample_spec.format);

        switch (output_.default_sample_spec.rate) {
            case 44100:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz44100;
                break;
            case 48000:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz44100;
                break;
            case 88200:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz88200;
                break;
            case 96000:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz96000;
                break;
            case 176400:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz176400;
                break;
            case 192000:
                shared_mode_sample_rate = AudioDeviceSampleRate::Hz192000;
                break;
        }

        open = true;

        return true;
    }

    /*
        Closes input and output devices after being used by the application.
    */
    void close_device() override {
        if (!open)
            return;
        if (running_) {
            pa_mainloop_quit(main_loop_, 0);
            audio_thread_.join();
            running_ = false;
            pa_stream_unref(output_stream_);
            free_aligned(conversion_buffer_);
            conversion_buffer_ = nullptr;
        }
        open = false;
        min_period = 0;
        buffer_alignment = 0;
    }

    /*
        Starts the audio engine.
        Audio thread will be launched here.
    */
    bool start(Engine* engine, bool exclusive_mode, uint32_t buffer_size, AudioFormat input_format,
               AudioFormat output_format, AudioDeviceSampleRate sample_rate,
               AudioThreadPriority priority) override {
        pa_sample_spec output_spec = to_sample_spec(output_format, sample_rate, 2);
        if (!pa_sample_spec_valid(&output_spec)) {
            return false;
        }

        pa_channel_map stereo_map;
        pa_channel_map_init_stereo(&stereo_map);
        if (!pa_channel_map_compatible(&stereo_map, &output_spec)) {
            return false;
        }

        pa_stream* output_stream =
            pa_stream_new(context_, "wb_pa_output_stream", &output_spec, &stereo_map);
        if (!output_stream) {
            return false;
        }

        int stream_flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
                           PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_UNMUTED |
                           PA_STREAM_NO_REMIX_CHANNELS | PA_STREAM_NO_REMAP_CHANNELS;

        uint32_t requested_buffer_size =
            buffer_size * output_spec.channels * get_audio_format_size(output_format);
        pa_buffer_attr output_buffer_attr {
            .maxlength = requested_buffer_size,
            .tlength = requested_buffer_size,
            .prebuf = (uint32_t)-1,
            .minreq = requested_buffer_size,
            .fragsize = (uint32_t)-1,
        };

        pa_stream_set_write_callback(output_stream, write_stream_callback, this);
        pa_stream_connect_playback(output_stream, output_.hw_name.c_str(), &output_buffer_attr,
                                   (pa_stream_flags)stream_flags, nullptr, nullptr);
        if (!wait_for_stream(output_stream)) {
            pa_stream_unref(output_stream);
            free_aligned(conversion_buffer_);
            return false;
        }

        output_stream_ = output_stream;
        output_sample_spec_ = output_spec;
        output_sample_format_ = output_format;
        output_buffer_.resize(buffer_size, true);
        output_buffer_.resize_channel(output_sample_spec_.channels);
        engine_ = engine;
        running_ = true;
        audio_thread_ = std::thread(audio_thread_runner, this, priority);

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

    bool wait_for_stream(pa_stream* stream) {
        while (true) {
            pa_mainloop_iterate(main_loop_, 1, nullptr);
            pa_stream_state state = pa_stream_get_state(stream);
            if (state == PA_STREAM_READY) {
                break;
            }
            if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
                return false;
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

    static void write_stream_callback(pa_stream* stream, size_t nbytes, void* userdata) {
        AudioIOPulseAudio2* current = (AudioIOPulseAudio2*)userdata;
        if (!current->running_.load(std::memory_order_relaxed)) {
            const pa_buffer_attr* actual_buffer_attr = pa_stream_get_buffer_attr(stream);
            uint32_t actual_buffer_size = actual_buffer_attr->tlength;
            uint32_t buffer_byte_size = actual_buffer_size;
            current->conversion_buffer_ = allocate_aligned(buffer_byte_size, 32);
            std::memset(current->conversion_buffer_, 0, buffer_byte_size);
            pa_stream_write(stream, current->conversion_buffer_, nbytes, nullptr, 0,
                            PA_SEEK_RELATIVE);
            return;
        }
        current->engine_->process(current->output_buffer_,
                                  (double)current->output_sample_spec_.rate);
        switch (current->output_sample_format_) {
            case AudioFormat::I16:
                convert_f32_to_interleaved_i16(
                    (int16_t*)current->conversion_buffer_, current->output_buffer_.channel_buffers,
                    0, current->output_buffer_.n_samples, current->output_buffer_.n_channels);
                break;
            case AudioFormat::I24:
                convert_f32_to_interleaved_i24((std::byte*)current->conversion_buffer_,
                                               current->output_buffer_.channel_buffers, 0,
                                               current->output_buffer_.n_samples,
                                               current->output_buffer_.n_channels);
                break;
            case AudioFormat::I24_X8:
                convert_f32_to_interleaved_i24_x8(
                    (int32_t*)current->conversion_buffer_, current->output_buffer_.channel_buffers,
                    0, current->output_buffer_.n_samples, current->output_buffer_.n_channels);
                break;
            case AudioFormat::I32:
                convert_f32_to_interleaved_i32(
                    (int32_t*)current->conversion_buffer_, current->output_buffer_.channel_buffers,
                    0, current->output_buffer_.n_samples, current->output_buffer_.n_channels);
                break;
            case AudioFormat::F32:
                convert_to_interleaved_f32(
                    (float*)current->conversion_buffer_, current->output_buffer_.channel_buffers, 0,
                    current->output_buffer_.n_samples, current->output_buffer_.n_channels);
                break;
            default:
                assert(false);
        }
        pa_stream_write(stream, current->conversion_buffer_, nbytes, nullptr, 0, PA_SEEK_RELATIVE);
    }

    static void audio_thread_runner(AudioIOPulseAudio2* instance,
                                    AudioThreadPriority thread_priority) {
        pa_mainloop_run(instance->main_loop_, nullptr);
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