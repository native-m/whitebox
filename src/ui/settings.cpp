#include "settings.h"
#include "engine/audio_io.h"
#include "settings_data.h"
#include <fmt/format.h>
#include <imgui.h>

namespace wb {
void GuiSettings::render() {
    if (!open)
        return;

    if (!ImGui::Begin("Settings", &open)) {
        ImGui::End();
    }

    if (ImGui::BeginTabBar("settings_tab")) {
        if (ImGui::BeginTabItem("General")) {
            ImGui::Button("Test");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Audio")) {
            static const char* io_types[] = {
                "Windows Core Audio",
            };

            static const char* sample_rates[] = {
                "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "176400 Hz", "192000 Hz",
            };

            static const char* buffer_sizes[] = {
                "32", "64", "128", "256", "512", "1024", "2048", "4096",
            };

            const char* io_type_preview = io_types[g_settings_data.audio_io_type];
            uint32_t output_count = g_audio_io->get_output_device_count();
            const AudioDeviceProperties& current_output_devprop =
                g_audio_io->get_output_device_properties(g_settings_data.audio_output_device_idx);

            if (ImGui::BeginCombo("Type", io_type_preview)) {
                for (uint32_t i = 0; i < IM_ARRAYSIZE(io_types); i++) {
                    const bool is_selected = i == g_settings_data.audio_io_type;
                    if (ImGui::Selectable(io_type_preview, is_selected))
                        g_settings_data.audio_io_type = i;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo("Output", current_output_devprop.name)) {
                for (uint32_t i = 0; i < output_count; i++) {
                    const bool is_selected = i == g_settings_data.audio_output_device_idx;
                    const AudioDeviceProperties& output_devprop =
                        g_audio_io->get_output_device_properties(i);

                    if (ImGui::Selectable(output_devprop.name, is_selected))
                        g_settings_data.audio_output_device_idx = i;

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

                ImGui::Checkbox("Exclusive mode", &g_settings_data.audio_exclusive_mode);

                if (g_settings_data.audio_exclusive_mode) {
                    if (ImGui::BeginCombo("Input format", input_format_str)) {
                        for (uint32_t i = 0; i < (uint32_t)AudioFormat::Max; i++) {
                            AudioFormat format = (AudioFormat)i;
                            if (!g_audio_io->is_input_format_supported(format))
                                continue;
                            const bool is_selected = i == current_input_format;
                            if (ImGui::Selectable(get_audio_format_string(format), is_selected))
                                g_settings_data.audio_input_format = format;
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
                            if (ImGui::Selectable(get_audio_format_string(format), is_selected))
                                g_settings_data.audio_output_format = format;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::BeginCombo("Sample rate", sample_rates[current_sample_rate_idx])) {
                    for (uint32_t i = 0; i < (uint32_t)AudioDeviceSampleRate::Max; i++) {
                        if (!g_audio_io->is_sample_rate_supported((AudioDeviceSampleRate)i))
                            continue;
                        const bool is_selected = i == current_sample_rate_idx;
                        if (ImGui::Selectable(sample_rates[i], is_selected))
                            g_settings_data.audio_sample_rate = (AudioDeviceSampleRate)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
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

                        if (ImGui::Selectable(tmp, is_selected))
                            g_settings_data.audio_buffer_size = min_buffer_size;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();

                        min_buffer_size += g_audio_io->buffer_alignment;
                    }
                    ImGui::EndCombo();
                }
            }

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