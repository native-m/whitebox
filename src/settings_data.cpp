#include "settings_data.h"
#include "core/debug.h"
#include "core/json.hpp"
#include "core/math.h"
#include "engine/audio_io.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace nlohmann;

namespace wb {

#ifdef NDEBUG
#if defined(WB_PLATFORM_WINDOWS)
static const std::filesystem::path userpath {std::getenv("USERPROFILE")};
#elif defined(WB_PLATFORM_LINUX)
static const std::filesystem::path userpath {std::getenv("HOME")};
#else
static const std::filesystem::path userpath {std::getenv("HOME")};
#endif
#else
static const std::filesystem::path userpath {std::filesystem::current_path()};
#endif

void SettingsData::load_settings_data() {
    Log::info("Loading user settings...");

    if (!std::filesystem::exists(userpath / ".whitebox" / "settings.json")) {
        Log::info("Creating default settings...");
        load_default_settings();
        return;
    }
}

void SettingsData::load_default_settings() {
#if defined(WB_PLATFORM_WINDOWS)
    audio_io_type = AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
    audio_io_type = AudioIOType::PulseAudio;
#else
    // ..
#endif

    init_audio_io(audio_io_type);
    output_device_properties = g_audio_io->default_output_device;
    input_device_properties = g_audio_io->default_input_device;

    g_audio_io->open_device(output_device_properties.id, input_device_properties.id);
    
    audio_sample_rate = g_audio_io->shared_mode_sample_rate;
    audio_input_format = g_audio_io->shared_mode_input_format;
    audio_output_format = g_audio_io->shared_mode_output_format;
    
    uint32_t sample_rate_value = get_sample_rate_value(audio_sample_rate);
    if (g_audio_io->min_period > buffer_size_to_period(512, sample_rate_value)) {
        audio_buffer_size = period_to_buffer_size(g_audio_io->min_period, sample_rate_value);
    } else {
        audio_buffer_size = 512;
    }

    g_audio_io->close_device();
    shutdown_audio_io();
}

void SettingsData::save_settings_data() {
    if (!std::filesystem::is_directory(userpath / ".whitebox")) {
        std::filesystem::create_directory(userpath / ".whitebox");
    }

    json audio;

    switch (audio_io_type) {
#ifdef WB_PLATFORM_WINDOWS
        case AudioIOType::WASAPI:
            audio.emplace("type", "wasapi");
            break;
#endif
#ifdef WB_PLATFORM_LINUX
        case AudioIOType::PulseAudio:
            audio.emplace("type", "pulseaudio");
            break;
#endif
    }

    audio.emplace("output_device_id", output_device_properties.id);
    audio.emplace("input_device_id", input_device_properties.id);
    audio.emplace("buffer_size", audio_buffer_size);

    json settings_json;
    settings_json.emplace("version", "0.0.1");
    settings_json.emplace("audio", audio);
}

void SettingsData::apply_audio_device() {
    g_audio_io->close_device();

    g_audio_io->open_device(output_device_properties.id, input_device_properties.id);
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