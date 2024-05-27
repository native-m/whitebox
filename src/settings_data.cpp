#include "settings_data.h"
#include "core/debug.h"
#include "core/json.hpp"
// #include "core/math.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
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
static const std::filesystem::path settings_file_path {userpath / ".whitebox" / "settings.json"};

void SettingsData::load_settings_data() {
    Log::info("Loading user settings...");

    if (!std::filesystem::exists(settings_file_path)) {
        Log::info("Creating default settings...");
        load_default_settings();
        return;
    }

    std::ifstream settings_file(settings_file_path);
    if (!settings_file.is_open()) {
        Log::error("Cannot read settings file. Creating default settings...");
        load_default_settings();
        return;
    }

    json settings = json::parse(settings_file, nullptr, false);
    if (settings.is_discarded()) {
        Log::error("Cannot parse settings file. Creating default settings...");
        load_default_settings();
        return;
    }

    AudioDeviceID output_device_id = 0;
    AudioDeviceID input_device_id = 0;
    uint32_t sample_rate_value = 0;
    if (settings.contains("audio")) {
        json audio = settings["audio"];

        AudioIOType default_type;
#if defined(WB_PLATFORM_WINDOWS)
        default_type = AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
        default_type = AudioIOType::PulseAudio;
#else
        // ..
#endif
        if (audio.contains("type")) {
            std::string audio_io_type_str = audio["type"].get<std::string>();
            if (audio_io_type_str == "wasapi") {
                audio_io_type = AudioIOType::WASAPI;
            } else {
                audio_io_type = default_type;
            }
        } else {
            audio_io_type = default_type;
        }
        if (audio.contains("output_device_id")) {
            output_device_id = audio["output_device_id"].get<AudioDeviceID>();
        }
        if (audio.contains("input_device_id")) {
            input_device_id = audio["input_device_id"].get<AudioDeviceID>();
        }
        if (audio.contains("buffer_size")) {
            audio_buffer_size = audio["buffer_size"].get<uint32_t>();
        }
        if (audio.contains("sample_rate")) {
            sample_rate_value = audio["sample_rate"].get<uint32_t>();
            switch (sample_rate_value) {
                case 44100:
                    audio_sample_rate = AudioDeviceSampleRate::Hz44100;
                    break;
                case 48000:
                    audio_sample_rate = AudioDeviceSampleRate::Hz48000;
                    break;
                case 88200:
                    audio_sample_rate = AudioDeviceSampleRate::Hz88200;
                    break;
                case 96000:
                    audio_sample_rate = AudioDeviceSampleRate::Hz96000;
                    break;
                case 176400:
                    audio_sample_rate = AudioDeviceSampleRate::Hz176400;
                    break;
                case 192000:
                    audio_sample_rate = AudioDeviceSampleRate::Hz192000;
                    break;
                default:
                    sample_rate_value = 0;
                    break;
            }
        }
    }

    init_audio_io(audio_io_type);
    uint32_t output_device_idx = g_audio_io->get_output_device_index(output_device_id);
    uint32_t input_device_idx = g_audio_io->get_input_device_index(input_device_id);
    output_device_properties = output_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                   ? g_audio_io->get_output_device_properties(output_device_idx)
                                   : g_audio_io->default_output_device;
    input_device_properties = input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                  ? g_audio_io->get_input_device_properties(input_device_idx)
                                  : g_audio_io->default_input_device;
    shutdown_audio_io();
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
    Log::info("Saving user settings...");

    if (!std::filesystem::is_directory(userpath / ".whitebox")) {
        std::filesystem::create_directory(userpath / ".whitebox");
    }

    ordered_json audio;

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

    uint32_t sample_rate_value = get_sample_rate_value(audio_sample_rate);
    audio["output_device_id"] = output_device_properties.id;
    audio["input_device_id"] = input_device_properties.id;
    audio["buffer_size"] = audio_buffer_size;
    audio["sample_rate"] = sample_rate_value;

    ordered_json settings_json;
    settings_json["version"] = "0.0.1";
    settings_json["audio"] = audio;

    std::ofstream settings_file(settings_file_path);
    if (settings_file.is_open()) {
        settings_file << settings_json.dump(4) << std::endl;
    } else {
        Log::error("Cannot write settings file");
    }
}

void SettingsData::apply_audio_settings() {
    shutdown_audio_io();
    init_audio_io(audio_io_type);
    g_audio_io->open_device(output_device_properties.id, input_device_properties.id);

    if (!audio_exclusive_mode) {
        audio_output_format = g_audio_io->shared_mode_output_format;
        audio_input_format = g_audio_io->shared_mode_input_format;
        audio_sample_rate = g_audio_io->shared_mode_sample_rate;
    }

    uint32_t sample_rate_value = get_sample_rate_value(g_settings_data.audio_sample_rate);
    AudioDevicePeriod period = buffer_size_to_period(audio_buffer_size, sample_rate_value);
    if (period < g_audio_io->min_period)
        audio_buffer_size = period_to_buffer_size(g_audio_io->min_period, sample_rate_value);
    // Realign buffer size
    audio_buffer_size += audio_buffer_size % g_audio_io->buffer_alignment;

    g_engine.set_buffer_size(2, audio_buffer_size);
    g_audio_io->start(&g_engine, audio_exclusive_mode, audio_buffer_size, audio_input_format,
                      audio_output_format, audio_sample_rate, AudioThreadPriority::Normal);
}

SettingsData g_settings_data;
} // namespace wb