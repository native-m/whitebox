#include "settings_data.h"
#include "core/math.h"
#include "engine/audio_io.h"

namespace wb {

void SettingsData::apply_audio_device() {
    g_audio_io->close_device();

    AudioDeviceProperties output_properties =
        g_audio_io->get_output_device_properties(audio_output_device_idx);
    AudioDeviceProperties input_properties =
        g_audio_io->get_input_device_properties(audio_input_device_idx);

    g_audio_io->open_device(output_properties.id, input_properties.id);
    if (!audio_exclusive_mode)
        audio_sample_rate = g_audio_io->shared_mode_sample_rate;

    apply_audio_buffer_size();
}

void SettingsData::apply_audio_buffer_size() {
    uint32_t sample_rate_value = get_sample_rate_value(g_settings_data.audio_sample_rate);
    AudioDevicePeriod period = buffer_size_to_period(audio_buffer_size, sample_rate_value);
    if (period < g_audio_io->min_period)
        audio_buffer_size = period_to_buffer_size(g_audio_io->min_period, sample_rate_value);
}

SettingsData g_settings_data;
} // namespace wb