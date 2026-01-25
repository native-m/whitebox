#include "config.h"

// #include "core/math.h"
#include <filesystem>
#include <fstream>

#include "app_event.h"
#include "core/debug.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/engine2.h"
#include "extern/json.hpp"
#include "path_def.h"
#include "ui/browser.h"

namespace wb {

static nlohmann::ordered_json settings;

AudioEngineConfig g_audio_engine_config;
AudioIOType g_audio_io_type{};
AudioDeviceProperties g_output_device_properties{};
AudioDeviceProperties g_input_device_properties{};
AudioDeviceSampleRate g_audio_sample_rate{};
AudioFormat g_audio_output_format{};
AudioFormat g_audio_input_format{};
uint32_t g_audio_buffer_size = 128;
bool g_audio_exclusive_mode = false;

void load_settings_data() {
  Log::info("Loading user settings...");

  if (!std::filesystem::is_directory(path_def::wbpath)) {
    std::filesystem::create_directory(path_def::wbpath);
  }

  if (!std::filesystem::exists(path_def::settings_json_path)) {
    Log::info("Creating default settings...");
    load_default_settings();
    return;
  }

  std::ifstream settings_file(path_def::settings_json_path);
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
  uint32_t buffer_size = 512;
  if (settings.contains("audio")) {
    nlohmann::ordered_json& audio = settings["audio"];

    AudioIOType default_type = AudioIO2::get_platform_recommended_audio_io_type();

    if (audio.contains("type")) {
      std::string audio_io_type_str = audio["type"].get<std::string>();
      if (audio_io_type_str == "wasapi") {
        Engine2::audio_io_type = AudioIOType::WASAPI;
      } else if (audio_io_type_str == "pulseaudio") {
        Engine2::audio_io_type = AudioIOType::PipeWire;
      } else {
        Engine2::audio_io_type = default_type;
      }
    } else {
      Engine2::audio_io_type = default_type;
    }

    if (audio.contains("input_device_id")) {
      Engine2::audio_engine_config.input_device_id = audio["input_device_id"].get<AudioDeviceID>();
    }

    if (audio.contains("output_device_id")) {
      Engine2::audio_engine_config.output_device_id = audio["output_device_id"].get<AudioDeviceID>();
    }

    if (audio.contains("buffer_size")) {
      buffer_size = audio["buffer_size"].get<uint32_t>();
    }

    Engine2::audio_engine_config.buffer_size = buffer_size;

    if (audio.contains("sample_rate")) {
      sample_rate_value = audio["sample_rate"].get<uint32_t>();
      switch (sample_rate_value) {
        case 44100: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz44100; break;
        case 48000: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz48000; break;
        case 88200: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz88200; break;
        case 96000: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz96000; break;
        case 176400: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz176400; break;
        case 192000: Engine2::audio_engine_config.sample_rate = AudioDeviceSampleRate::Hz192000; break;
        default: Engine2::audio_engine_config.sample_rate = {}; break;
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
}

void load_default_settings() {
  Engine2::reset_audio_engine_config();
}

void save_settings_data() {
  Log::info("Saving user settings...");

  if (!std::filesystem::is_directory(path_def::wbpath)) {
    std::filesystem::create_directory(path_def::wbpath);
  }

  settings["version"] = "0.0.2";

  switch (Engine2::audio_io_type) {
    case AudioIOType::NoAudio: settings["audio"]["type"] = "no_audio"; break;
#ifdef WB_PLATFORM_WINDOWS
    case AudioIOType::WASAPI: settings["audio"]["type"] = "wasapi"; break;
#endif
#ifdef WB_PLATFORM_LINUX
    case AudioIOType::PipeWire: settings["audio"]["type"] = "pipewire"; break;
#endif
#ifdef WB_PLATFORM_MACOS
    case AudioIOType::CoreAudio: settings["audio"]["type"] = "coreaudio"; break;
#endif
    default: break;
  }

  uint32_t sample_rate_value = get_sample_rate_value(Engine2::audio_engine_config.sample_rate);
  settings["audio"]["output_device_id"] = Engine2::audio_engine_config.output_device_id;
  settings["audio"]["input_device_id"] = Engine2::audio_engine_config.input_device_id;
  settings["audio"]["buffer_size"] = Engine2::audio_engine_config.buffer_size;
  settings["audio"]["sample_rate"] = sample_rate_value;

  std::vector<std::string> user_dirs;
  user_dirs.reserve(g_browser.directories.size());
  for (const auto& dir : g_browser.directories) {
    user_dirs.push_back(dir.first->generic_string());
  }
  settings["user_dirs"] = user_dirs;

  std::ofstream settings_file(path_def::settings_json_path);
  if (settings_file.is_open()) {
    settings_file << settings.dump(2) << std::endl;
  } else {
    Log::error("Cannot write settings file");
  }
}

}  // namespace wb