#pragma once

#include "core/common.h"
#include "engine/audio_io.h"

namespace wb {
struct SettingsData {
    AudioIOType audio_io_type {};
    AudioDeviceProperties output_device_properties {};
    AudioDeviceProperties input_device_properties {};
    AudioDeviceSampleRate audio_sample_rate {};
    AudioFormat audio_output_format {};
    AudioFormat audio_input_format {};
    uint32_t audio_buffer_size = 128;
    bool audio_exclusive_mode = false;

    void load_settings_data();
    void load_default_settings();
    void save_settings_data();
    void apply_audio_settings();
};

extern SettingsData g_settings_data;
} // namespace wb