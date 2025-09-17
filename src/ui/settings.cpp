#include "settings.h"

#include <fmt/format.h>
#include <imgui.h>

#include "app_event.h"
#include "config.h"
#include "engine/audio_io.h"
#include "engine/engine2.h"
#include "window.h"

static const char* io_types[] = {
  "(No Audio)", "Windows Core Audio (WASAPI)", "ASIO", "CoreAudio", "PulseAudio",
};

static const char* sample_rates[] = {
  "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "176400 Hz", "192000 Hz",
};

static const char* buffer_sizes[] = {
  "32", "64", "128", "256", "512", "1024", "2048", "4096",
};

namespace wb {

static void render_audio_settings();

void render_settings() {
  ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings", &g_settings_window_open, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("settings_tab")) {
    if (ImGui::BeginTabItem("General")) {
      ImGui::Button("Test");
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Audio")) {
      render_audio_settings();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("MIDI")) {
      ImGui::Button("TODO");
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();
}

void render_audio_settings() {
  uint32_t io_type_index = static_cast<uint32_t>(Engine2::audio_io_type);
  const char* io_type_preview = io_types[io_type_index];
  AudioDeviceProperties& current_output_devprop = Engine2::output_device_properties;
  AudioDeviceProperties& current_input_devprop = Engine2::input_device_properties;
  bool audio_io_type_changed = false;
  bool audio_settings_changed = false;

  if (ImGui::BeginCombo("Driver Type", io_type_preview)) {
    for (uint32_t i = 0; i < IM_ARRAYSIZE(io_types); i++) {
      AudioIOType type = static_cast<AudioIOType>(i);

      // Skip unsupported platform audio I/O
      if (type != AudioIOType::NoAudio) {
#if defined(WB_PLATFORM_WINDOWS)
        if (type != AudioIOType::WASAPI)
          continue;
#elif defined(WB_PLATFORM_LINUX)
        if (type != AudioIOType::PulseAudio)
          continue;
#elif defined(WB_PLATFORM_MACOS)
        if (type != AudioIOType::CoreAudio)
          continue;
#endif
      }

      const bool is_selected = i == io_type_index;
      if (ImGui::Selectable(io_types[i], is_selected)) {
        if (!is_selected)
          audio_io_type_changed = true;
        Engine2::audio_io_type = static_cast<AudioIOType>(i);
      }

      if (is_selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (auto audio_io = Engine2::audio_io) {
    if (ImGui::BeginCombo("Input", current_input_devprop.name)) {
      for (uint32_t i = 0; i < audio_io->get_input_device_count(); i++) {
        const AudioDeviceProperties& device_properties = audio_io->get_input_device_properties(i);
        const bool is_selected = device_properties.id == current_input_devprop.id;

        if (ImGui::Selectable(device_properties.name, is_selected)) {
          if (!is_selected)
            audio_settings_changed = true;
          current_input_devprop = device_properties;
          Engine2::audio_engine_config.input_device_id = device_properties.id;
        }

        if (is_selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Output", current_output_devprop.name)) {
      for (uint32_t i = 0; i < audio_io->get_output_device_count(); i++) {
        const AudioDeviceProperties& device_properties = audio_io->get_output_device_properties(i);
        const bool is_selected = device_properties.id == current_output_devprop.id;

        if (ImGui::Selectable(device_properties.name, is_selected)) {
          if (!is_selected)
            audio_settings_changed = true;
          current_output_devprop = device_properties;
          Engine2::audio_engine_config.output_device_id = device_properties.id;
        }

        if (is_selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (audio_io->is_device_open()) {
      uint32_t current_sample_rate_idx = (uint32_t)Engine2::audio_engine_config.sample_rate;
      uint32_t current_input_format = (uint32_t)Engine2::audio_engine_config.input_format;
      uint32_t current_output_format = (uint32_t)Engine2::audio_engine_config.output_format;
      uint32_t current_buffer_size = Engine2::audio_engine_config.buffer_size;
      uint32_t current_sample_rate_value = get_sample_rate_value(Engine2::audio_engine_config.sample_rate);
      const char* input_format_str = get_audio_format_string(Engine2::audio_engine_config.input_format);
      const char* output_format_str = get_audio_format_string(Engine2::audio_engine_config.output_format);

      ImGui::BeginDisabled();
      ImGui::Checkbox("Exclusive mode", &Engine2::audio_engine_config.exclusive_mode);
      ImGui::EndDisabled();

      if (g_audio_exclusive_mode) {
        if (ImGui::BeginCombo("Input format", input_format_str)) {
          for (uint32_t i = 0; i < (uint32_t)AudioFormat::Max; i++) {
            AudioFormat format = (AudioFormat)i;
            if (!audio_io->is_input_sample_format_supported(format))
              continue;
            const bool is_selected = i == current_input_format;
            if (ImGui::Selectable(get_audio_format_string(format), is_selected)) {
              if (!is_selected)
                audio_settings_changed = true;
              Engine2::audio_engine_config.input_format = format;
            }
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Output format", output_format_str)) {
          for (uint32_t i = 0; i < (uint32_t)AudioFormat::Max; i++) {
            AudioFormat format = (AudioFormat)i;
            if (!audio_io->is_output_sample_format_supported(format))
              continue;
            const bool is_selected = i == current_output_format;
            if (ImGui::Selectable(get_audio_format_string(format), is_selected)) {
              if (!is_selected)
                audio_settings_changed = true;
              Engine2::audio_engine_config.output_format = format;
            }
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Sample rate", sample_rates[current_sample_rate_idx])) {
          for (uint32_t i = 0; i < (uint32_t)AudioDeviceSampleRate::Max; i++) {
            if (!audio_io->is_sample_rate_supported((AudioDeviceSampleRate)i))
              continue;
            const bool is_selected = i == current_sample_rate_idx;
            if (ImGui::Selectable(sample_rates[i], is_selected)) {
              if (!is_selected)
                audio_settings_changed = true;
              Engine2::audio_engine_config.sample_rate = (AudioDeviceSampleRate)i;
            }
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }

      char tmp[256];
      AudioDevicePeriod current_period = buffer_size_to_period(current_buffer_size, current_sample_rate_value);
      double current_period_ms = period_to_ms(current_period);

      const char* buf;
      ImFormatStringToTempBuffer(&buf, nullptr, "%u (%.2f ms)", current_buffer_size, current_period_ms);

      // TODO: Replace this with slider/drag
      if (ImGui::BeginCombo("Buffer Size", buf)) {
        uint32_t min_buffer_size = period_to_buffer_size(audio_io->min_period, current_sample_rate_value);

        while (min_buffer_size <= 4096) {
          const bool is_selected = min_buffer_size == current_buffer_size;
          double period_ms = period_to_ms(buffer_size_to_period(min_buffer_size, current_sample_rate_value));

          ImFormatStringToTempBuffer(&buf, nullptr, "%u (%.2f ms)", min_buffer_size, period_ms);

          if (ImGui::Selectable(buf, is_selected)) {
            if (!is_selected)
              audio_settings_changed = true;
            Engine2::audio_engine_config.buffer_size = min_buffer_size;
          }
          if (is_selected)
            ImGui::SetItemDefaultFocus();

          // Align next size
          uint32_t size_rem = min_buffer_size % audio_io->buffer_alignment;
          if (size_rem == 0) {
            min_buffer_size += audio_io->buffer_alignment;
          } else {
            min_buffer_size += size_rem;
          }
        }

        ImGui::EndCombo();
      }
    }
  }

  if (audio_io_type_changed) {
    app_event_push(AppEvent::audio_io_type_changed);
  } else if (audio_settings_changed) {
    app_event_push(AppEvent::audio_settings_changed);
  }
}

}  // namespace wb