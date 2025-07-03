#pragma once

#include "core/common.h"
#include "engine/audio_io.h"

namespace wb {

extern AudioIOType g_audio_io_type;
extern AudioDeviceProperties g_output_device_properties;
extern AudioDeviceProperties g_input_device_properties;
extern AudioDeviceSampleRate g_audio_sample_rate;
extern AudioFormat g_audio_output_format;
extern AudioFormat g_audio_input_format;
extern uint32_t g_audio_buffer_size;
extern bool g_audio_exclusive_mode;

void load_settings_data();
void load_default_settings();
void save_settings_data();
void start_audio_engine();

}  // namespace wb