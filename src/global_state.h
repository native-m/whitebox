#pragma once

#include "engine/audio_stream.h"
#include "gui_timeline.h"
#include <unordered_map>

namespace wb
{
    extern int g_audio_driver_type;
    extern int g_output_device;
    extern int g_input_device;
    extern int g_output_audio_mode;
    extern int g_input_audio_mode;
    extern uint32_t g_audio_buffer_size;

    extern int g_default_resampler_mode;
    extern int g_default_resampler_param;

    extern bool g_settings_window_open;
    extern bool g_show_timeline_window;
    extern bool g_show_content_browser;
    extern bool g_should_scan_audio_device;
    extern std::vector<AudioDeviceProperties> g_output_devices;
    extern std::vector<AudioDeviceProperties> g_input_devices;
    extern AudioModeString g_output_modes;
    extern AudioModeString g_input_modes;

    extern Track* g_selected_track;
    extern Clip* g_selected_clip;
    extern uint32_t g_last_new_track_n;
    extern Engine g_engine;

    void update_audio_device_list();
    void update_audio_mode_list();
    void set_audio_mode_to_default();
    void apply_audio_devices();
    void try_start_audio_stream();
    double get_output_sample_rate();
    void render_settings_ui();
}