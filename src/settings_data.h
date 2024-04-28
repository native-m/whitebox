#pragma once

#include "core/common.h"
#include "engine/audio_io.h"

namespace wb {
struct SettingsData {
    uint32_t audio_io_type = 0;
    uint32_t audio_input_device_idx = 0;
    uint32_t audio_output_device_idx = 0;
    AudioDeviceSampleRate audio_sample_rate {};
    AudioFormat audio_output_format {};
    AudioFormat audio_input_format {};
    uint32_t audio_buffer_size = 128;
    bool audio_exclusive_mode = false;

    void save_settings_data();
    void apply_audio_device();
    void apply_audio_buffer_size();
};

extern SettingsData g_settings_data;
} // namespace wb