#pragma once

#include "core/audio_format.h"
#include "core/bit_manipulation.h"
#include "core/common.h"
#include <cmath>
#include <string>

namespace wb {

using AudioDeviceID = uint64_t;
using AudioDevicePeriod = int64_t;

enum class AudioIOType {
    None,
    CoreAudio, // Soon
    WASAPI,
    ASIO,
};

enum class AudioDeviceType {
    Input,
    Output,
};

enum class AudioDeviceSampleRate {
    Hz44100,
    Hz48000,
    Hz88200,
    Hz96000,
    Hz176400,
    Hz192000,
    Max,
};

struct AudioDeviceProperties {
    char name[128] {};
    AudioDeviceID id;
    AudioDeviceType type;
    AudioIOType io_type;
};

struct AudioDeviceFormat {
    AudioDeviceSampleRate sample_rate;
    AudioFormat output_sample_format;
    uint16_t output_channels;
    uint16_t input_channels;
};

struct AudioIO {
    AudioDeviceProperties default_input_device;
    AudioDeviceProperties default_output_device;
    uint32_t input_device_count = 0;
    uint32_t output_device_count = 0;
    uint32_t exclusive_sample_rate_mask = 0;
    uint32_t exclusive_input_format_mask = 0;
    uint32_t exclusive_output_format_mask = 0;
    AudioDevicePeriod min_period = 0;
    uint32_t buffer_alignment = 0;
    bool open = false;

    uint32_t get_input_device_count() const { return input_device_count; }
    uint32_t get_output_device_count() const { return output_device_count; }
    bool is_open() const { return open; }

    bool is_sample_rate_supported(AudioDeviceSampleRate sample_rate) const {
        return has_bit_enum(exclusive_sample_rate_mask, sample_rate);
    }

    bool is_input_format_supported(AudioFormat format) const {
        return has_bit_enum(exclusive_input_format_mask, format);
    }

    bool is_output_format_supported(AudioFormat format) const {
        return has_bit_enum(exclusive_output_format_mask, format);
    }

    virtual ~AudioIO() {}
    virtual bool rescan_devices() = 0;
    virtual const AudioDeviceProperties& get_input_device_properties(uint32_t idx) const = 0;
    virtual const AudioDeviceProperties& get_output_device_properties(uint32_t idx) const = 0;
    virtual bool open_device(AudioDeviceID output_device_id, AudioDeviceID input_device_idx) = 0;
    virtual void close_device() = 0;
    virtual bool start(bool exclusive_mode, AudioDevicePeriod period, AudioFormat input_format,
                       AudioFormat output_format, AudioDeviceSampleRate sample_rate) = 0;
    virtual void end() = 0;
};

inline static uint32_t period_to_buffer_size(AudioDevicePeriod period, uint32_t sample_rate) {
    constexpr double unit_100_ns = 10000000.0;
    return (uint32_t)std::round(sample_rate * period / unit_100_ns);
}

inline static double period_to_ms(AudioDevicePeriod period) {
    constexpr double unit_100_ns = 10000000.0;
    return 1000.0 * period / unit_100_ns;
}

inline static AudioDevicePeriod buffer_size_to_period(uint32_t buffer_size, uint32_t sample_rate) {
    constexpr double unit_100_ns = 10000000.0;
    return (AudioDevicePeriod)std::round(unit_100_ns * (buffer_size / (double)sample_rate));
}

inline static uint32_t get_sample_rate_value(AudioDeviceSampleRate sr_enum) {
    switch (sr_enum) {
        case AudioDeviceSampleRate::Hz44100:
            return 44100;
        case AudioDeviceSampleRate::Hz48000:
            return 48000;
        case AudioDeviceSampleRate::Hz88200:
            return 88200;
        case AudioDeviceSampleRate::Hz96000:
            return 96000;
        case AudioDeviceSampleRate::Hz176400:
            return 176400;
        case AudioDeviceSampleRate::Hz192000:
            return 192000;
        default:
            break;
    }
    return 0;
}

void init_audio_io(AudioIOType type);
void shutdown_audio_io();

extern AudioIO* g_audio_io;

} // namespace wb