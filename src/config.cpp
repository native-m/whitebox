#include "config.h"
#include "app.h"
#include "core/debug.h"
#include "extern/json.hpp"
// #include "core/math.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "ui/browser.h"
#include <filesystem>
#include <fstream>

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
static const std::filesystem::path devpath {std::filesystem::current_path()};
static const std::filesystem::path settings_file_path {devpath / ".whitebox" / "settings.json"};
static nlohmann::ordered_json settings;

AudioIOType g_audio_io_type {};
AudioDeviceProperties g_output_device_properties {};
AudioDeviceProperties g_input_device_properties {};
AudioDeviceSampleRate g_audio_sample_rate {};
AudioFormat g_audio_output_format {};
AudioFormat g_audio_input_format {};
uint32_t g_audio_buffer_size = 128;
bool g_audio_exclusive_mode = false;

void load_settings_data() {
    Log::info("Loading user settings...");

    if (!std::filesystem::is_directory(devpath / ".whitebox")) {
        std::filesystem::create_directory(devpath / ".whitebox");
    }

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

    settings = nlohmann::ordered_json::parse(settings_file, nullptr, false);
    if (settings.is_discarded()) {
        Log::error("Cannot parse settings file. Creating default settings...");
        load_default_settings();
        return;
    }

    AudioDeviceID output_device_id = 0;
    AudioDeviceID input_device_id = 0;
    uint32_t sample_rate_value = 0;
    if (settings.contains("audio")) {
        nlohmann::ordered_json& audio = settings["audio"];

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
                g_audio_io_type = AudioIOType::WASAPI;
            } else {
                g_audio_io_type = default_type;
            }
        } else {
            g_audio_io_type = default_type;
        }
        if (audio.contains("output_device_id")) {
            output_device_id = audio["output_device_id"].get<AudioDeviceID>();
        }
        if (audio.contains("input_device_id")) {
            input_device_id = audio["input_device_id"].get<AudioDeviceID>();
        }
        if (audio.contains("buffer_size")) {
            g_audio_buffer_size = audio["buffer_size"].get<uint32_t>();
        }
        if (audio.contains("sample_rate")) {
            sample_rate_value = audio["sample_rate"].get<uint32_t>();
            switch (sample_rate_value) {
                case 44100:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz44100;
                    break;
                case 48000:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz48000;
                    break;
                case 88200:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz88200;
                    break;
                case 96000:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz96000;
                    break;
                case 176400:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz176400;
                    break;
                case 192000:
                    g_audio_sample_rate = AudioDeviceSampleRate::Hz192000;
                    break;
                default:
                    g_audio_sample_rate = {};
                    break;
            }
        }
    }

    if (settings.contains("user_dirs")) {
        nlohmann::ordered_json& user_dirs = settings["user_dirs"];
        if (user_dirs.is_array()) {
            for (auto& dir : user_dirs) {
                if (dir.is_string()) {
                    g_browser.add_directory(dir.get<std::string>());
                }
            }
            g_browser.sort_directory();
        }
    }

    init_audio_io(g_audio_io_type);
    uint32_t output_device_idx = g_audio_io->get_output_device_index(output_device_id);
    uint32_t input_device_idx = g_audio_io->get_input_device_index(input_device_id);
    g_output_device_properties = output_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                     ? g_audio_io->get_output_device_properties(output_device_idx)
                                     : g_audio_io->default_output_device;
    g_input_device_properties = input_device_idx != WB_INVALID_AUDIO_DEVICE_INDEX
                                    ? g_audio_io->get_input_device_properties(input_device_idx)
                                    : g_audio_io->default_input_device;
    shutdown_audio_io();
}

void load_default_settings() {
#if defined(WB_PLATFORM_WINDOWS)
    g_audio_io_type = AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
    g_audio_io_type = AudioIOType::PulseAudio;
#else
    // ..
#endif

    init_audio_io(g_audio_io_type);
    g_output_device_properties = g_audio_io->default_output_device;
    g_input_device_properties = g_audio_io->default_input_device;

    g_audio_io->open_device(g_output_device_properties.id, g_input_device_properties.id);

    g_audio_sample_rate = g_audio_io->shared_mode_sample_rate;
    g_audio_input_format = g_audio_io->shared_mode_input_format;
    g_audio_output_format = g_audio_io->shared_mode_output_format;

    static constexpr uint32_t default_buffer_size = 512;
    uint32_t sample_rate_value = get_sample_rate_value(g_audio_sample_rate);
    if (g_audio_io->min_period > buffer_size_to_period(default_buffer_size, sample_rate_value)) {
        g_audio_buffer_size = period_to_buffer_size(g_audio_io->min_period, sample_rate_value);
    } else {
        g_audio_buffer_size = default_buffer_size;
    }

    g_audio_io->close_device();
    shutdown_audio_io();
}

void save_settings_data() {
    Log::info("Saving user settings...");

    if (!std::filesystem::is_directory(devpath / ".whitebox")) {
        std::filesystem::create_directory(devpath / ".whitebox");
    }

    settings["version"] = "0.0.2";

    switch (g_audio_io_type) {
#ifdef WB_PLATFORM_WINDOWS
        case AudioIOType::WASAPI:
            settings["audio"]["type"] = "wasapi";
            break;
#endif
#ifdef WB_PLATFORM_LINUX
        case AudioIOType::PulseAudio:
            settings["audio"]["type"] = "pulseaudio";
            break;
#endif
        default:
            break;
    }

    uint32_t sample_rate_value = get_sample_rate_value(g_audio_sample_rate);
    settings["audio"]["output_device_id"] = g_output_device_properties.id;
    settings["audio"]["input_device_id"] = g_output_device_properties.id;
    settings["audio"]["buffer_size"] = g_audio_buffer_size;
    settings["audio"]["sample_rate"] = sample_rate_value;

    std::vector<std::string> user_dirs;
    user_dirs.reserve(g_browser.directories.size());
    for (const auto& dir : g_browser.directories) {
        user_dirs.push_back(dir.first->generic_string());
    }
    settings["user_dirs"] = user_dirs;

    std::ofstream settings_file(settings_file_path);
    if (settings_file.is_open()) {
        settings_file << settings.dump(2) << std::endl;
    } else {
        Log::error("Cannot write settings file");
    }
}

void on_device_removed_callback(void* userdata) {
    app_push_event(AppEvent::audio_device_removed_event, nullptr, 0);
}

void start_audio_engine() {
    shutdown_audio_io();
    init_audio_io(g_audio_io_type);
    if (!g_audio_io->open_device(g_output_device_properties.id, g_input_device_properties.id)) {
        // Set default audio device
        g_input_device_properties = g_audio_io->default_input_device;
        g_output_device_properties = g_audio_io->default_output_device;
        g_audio_io->open_device(g_output_device_properties.id, g_input_device_properties.id);
    }
    g_audio_io->set_on_device_removed_cb(on_device_removed_callback);

    if (!g_audio_exclusive_mode) {
        g_audio_output_format = g_audio_io->shared_mode_output_format;
        g_audio_input_format = g_audio_io->shared_mode_input_format;
        g_audio_sample_rate = g_audio_io->shared_mode_sample_rate;
    }

    uint32_t sample_rate_value = get_sample_rate_value(g_audio_sample_rate);
    AudioDevicePeriod period = buffer_size_to_period(g_audio_buffer_size, sample_rate_value);
    if (period < g_audio_io->min_period)
        g_audio_buffer_size = period_to_buffer_size(g_audio_io->min_period, sample_rate_value);
    // Realign buffer size
    g_audio_buffer_size -= g_audio_buffer_size % g_audio_io->buffer_alignment;

    g_engine.set_buffer_size(2, g_audio_buffer_size);
    g_audio_io->start(&g_engine, g_audio_exclusive_mode, g_audio_buffer_size, g_audio_input_format,
                      g_audio_output_format, g_audio_sample_rate, AudioThreadPriority::High);
}
} // namespace wb