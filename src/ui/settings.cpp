#include "settings.h"
#include "engine/audio_io.h"
#include "settings_data.h"
#include <fmt/format.h>
#include <imgui.h>

static const char* io_types[] = {
    "Windows Core Audio (WASAPI)",
    "ASIO",
    "CoreAudio",
    "PulseAudio",
};

static const char* sample_rates[] = {
    "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "176400 Hz", "192000 Hz",
};

static const char* buffer_sizes[] = {
    "32", "64", "128", "256", "512", "1024", "2048", "4096",
};

namespace wb {
void GuiSettings::render() {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &open)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("settings_tab")) {
        if (ImGui::BeginTabItem("General")) {
            ImGui::Button("Test");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Audio")) {
            uint32_t io_type_index = static_cast<uint32_t>(g_settings_data.audio_io_type);
            const char* io_type_preview = io_types[io_type_index];
            uint32_t output_count = g_audio_io->get_output_device_count();
            const AudioDeviceProperties& current_output_devprop =
                g_settings_data.output_device_properties;

            bool audio_io_type_changed = false;
            bool audio_settings_changed = false;

            if (ImGui::BeginCombo("Type", io_type_preview)) {
                for (uint32_t i = 0; i < IM_ARRAYSIZE(io_types); i++) {
                    AudioIOType type = static_cast<AudioIOType>(i);

                    // Skip unsupported platform audio I/O
#if defined(WB_PLATFORM_WINDOWS)
                    if (type != AudioIOType::WASAPI)
                        continue;
#elif defined(WB_PLATFORM_LINUX)
                    if (type != AudioIOType::PulseAudio)
                        continue;
#endif
                    const bool is_selected = i == io_type_index;
                    if (ImGui::Selectable(io_types[i], is_selected)) {
                        if (!is_selected)
                            audio_settings_changed = true;
                        g_settings_data.audio_io_type = static_cast<AudioIOType>(i);
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo("Output", current_output_devprop.name)) {
                for (uint32_t i = 0; i < output_count; i++) {
                    const AudioDeviceProperties& device_properties =
                        g_audio_io->get_output_device_properties(i);
                    const bool is_selected =
                        device_properties.id == g_settings_data.output_device_properties.id;

                    if (ImGui::Selectable(device_properties.name, is_selected)) {
                        if (!is_selected)
                            audio_settings_changed = true;
                        g_settings_data.output_device_properties = device_properties;
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (g_audio_io->is_open()) {
                uint32_t current_sample_rate_idx = (uint32_t)g_settings_data.audio_sample_rate;
                uint32_t current_input_format = (uint32_t)g_settings_data.audio_input_format;
                uint32_t current_output_format = (uint32_t)g_settings_data.audio_output_format;
                uint32_t current_buffer_size = g_settings_data.audio_buffer_size;
                uint32_t current_sample_rate_value =
                    get_sample_rate_value(g_settings_data.audio_sample_rate);
                const char* input_format_str =
                    get_audio_format_string(g_settings_data.audio_input_format);
                const char* output_format_str =
                    get_audio_format_string(g_settings_data.audio_output_format);

                ImGui::BeginDisabled();
                ImGui::Checkbox("Exclusive mode", &g_settings_data.audio_exclusive_mode);
                ImGui::EndDisabled();

                if (g_settings_data.audio_exclusive_mode) {
                    if (ImGui::BeginCombo("Input format", input_format_str)) {
                        for (uint32_t i = 0; i < (uint32_t)AudioFormat::Max; i++) {
                            AudioFormat format = (AudioFormat)i;
                            if (!g_audio_io->is_input_format_supported(format))
                                continue;
                            const bool is_selected = i == current_input_format;
                            if (ImGui::Selectable(get_audio_format_string(format), is_selected)) {
                                if (!is_selected)
                                    audio_settings_changed = true;
                                g_settings_data.audio_input_format = format;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginCombo("Output format", output_format_str)) {
                        for (uint32_t i = 0; i < (uint32_t)AudioFormat::Max; i++) {
                            AudioFormat format = (AudioFormat)i;
                            if (!g_audio_io->is_output_format_supported(format))
                                continue;
                            const bool is_selected = i == current_output_format;
                            if (ImGui::Selectable(get_audio_format_string(format), is_selected)) {
                                if (!is_selected)
                                    audio_settings_changed = true;
                                g_settings_data.audio_output_format = format;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginCombo("Sample rate", sample_rates[current_sample_rate_idx])) {
                        for (uint32_t i = 0; i < (uint32_t)AudioDeviceSampleRate::Max; i++) {
                            if (!g_audio_io->is_sample_rate_supported((AudioDeviceSampleRate)i))
                                continue;
                            const bool is_selected = i == current_sample_rate_idx;
                            if (ImGui::Selectable(sample_rates[i], is_selected)) {
                                if (!is_selected)
                                    audio_settings_changed = true;
                                g_settings_data.audio_sample_rate = (AudioDeviceSampleRate)i;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                char tmp[256];
                AudioDevicePeriod current_period =
                    buffer_size_to_period(current_buffer_size, current_sample_rate_value);
                double current_period_ms = period_to_ms(current_period);

                std::snprintf(tmp, sizeof(tmp), "%u (%.2f ms)", current_buffer_size,
                              current_period_ms);

                if (ImGui::BeginCombo("Buffer Size", tmp)) {
                    uint32_t min_buffer_size =
                        period_to_buffer_size(g_audio_io->min_period, current_sample_rate_value);

                    while (min_buffer_size <= 4096) {
                        const bool is_selected = min_buffer_size == current_buffer_size;
                        double period_ms = period_to_ms(
                            buffer_size_to_period(min_buffer_size, current_sample_rate_value));

                        std::snprintf(tmp, sizeof(tmp), "%u (%.2f ms)", min_buffer_size, period_ms);

                        if (ImGui::Selectable(tmp, is_selected)) {
                            if (!is_selected)
                                audio_settings_changed = true;
                            g_settings_data.audio_buffer_size = min_buffer_size;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();

                        // Align next size
                        uint32_t size_rem = min_buffer_size % g_audio_io->buffer_alignment;
                        if (size_rem == 0) {
                            min_buffer_size += g_audio_io->buffer_alignment;
                        } else {
                            min_buffer_size += size_rem;
                        }
                    }

                    ImGui::EndCombo();
                }
            }

            if (audio_settings_changed)
                g_settings_data.apply_audio_settings();

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

GuiSettings g_settings;
} // namespace wb