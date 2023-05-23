#include "global_state.h"
#include "core/debug.h"
#include "def.h"
#include <imgui.h>
#include <ranges>

namespace wb
{
    int g_audio_driver_type = 0;
    int g_output_device = -1;
    int g_input_device = -1;
    int g_output_audio_mode = 0;
    int g_input_audio_mode = 0;
    uint32_t g_audio_buffer_size = 512;
    int g_default_resampler_mode = 0;
    int g_default_resampler_param = 0;

    bool g_settings_window_open = false;
    bool g_show_timeline_window = true;
    bool g_show_content_browser = true;
    bool g_should_scan_audio_device = true;
    std::vector<AudioDeviceProperties> g_output_devices;
    std::vector<AudioDeviceProperties> g_input_devices;
    AudioModeString g_output_modes;
    AudioModeString g_input_modes;

    Track* g_selected_track;
    Clip* g_selected_clip;
    uint32_t g_last_new_track_n = 0;
    Engine g_engine;

    auto get_audio_modes(AudioDeviceType type, bool exclusive_mode)
    {
        std::vector<std::pair<AudioMode, char[128]>> audio_modes;
        auto formats = { AudioFormat::I8, AudioFormat::I16, AudioFormat::I24, AudioFormat::I32, AudioFormat::F32 };
        auto channels = { 1, 2 };
        auto sample_rates = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };

        for (auto format : formats) {
            for (auto n_channels : channels) {
                for (auto sample_rate : sample_rates) {
                    AudioMode mode{
                        .format = format,
                        .channels = (uint16_t)n_channels,
                        .sample_rate = (uint32_t)sample_rate,
                    };
                    bool supported = type == AudioDeviceType::Input ?
                        ae_check_input_mode_support(exclusive_mode, mode) :
                        ae_check_output_mode_support(exclusive_mode, mode);

                    if (supported) {
                        std::pair<AudioMode, char[128]> supported_mode{};
                        const char* format_str = "Unknown";
                        const char* n_channels_str = "Unknown";

                        switch (format) {
                            case AudioFormat::I8:   format_str = "8-bit"; break;
                            case AudioFormat::I16:  format_str = "16-bit"; break;
                            case AudioFormat::I24:  format_str = "24-bit"; break;
                            case AudioFormat::I32:  format_str = "32-bit"; break;
                            case AudioFormat::F32:  format_str = "32-bit Float"; break;
                            default:                WB_UNREACHABLE();
                        }

                        switch (n_channels) {
                            case 1:     n_channels_str = "Mono"; break;
                            case 2:     n_channels_str = "Stereo"; break;
                            default:    WB_UNREACHABLE();
                        }

                        fmt::format_to_n(supported_mode.second, 128, "{}, {}, {} Hz", format_str, n_channels_str, sample_rate);
                        supported_mode.first = mode;
                        audio_modes.push_back(supported_mode);
                    }
                }
            }
        }

        return audio_modes;
    };

    int get_default_audio_mode(const AudioModeString& audio_mode_str)
    {
        uint32_t channels = 2;
        uint32_t sample_rate = 44100;
        int index = (int)audio_mode_str.size() - 1;

        for (auto& mode : std::views::reverse(audio_mode_str)) {
            if (channels >= mode.first.channels && sample_rate >= mode.first.sample_rate)
                break;
            index--;
        }

        return index;
    }

    void update_audio_device_list()
    {
        Log::info("Scanning Audio Interface...");
        g_input_devices = ae_get_input_devices();
        g_output_devices = ae_get_output_devices();
    }

    void update_audio_mode_list()
    {
        g_output_modes = get_audio_modes(AudioDeviceType::Output, false);
        g_input_modes = get_audio_modes(AudioDeviceType::Input, false);
    }

    void set_audio_mode_to_default()
    {
        g_output_audio_mode = get_default_audio_mode(g_output_modes);
        g_input_audio_mode = get_default_audio_mode(g_input_modes);
    }

    void apply_audio_devices()
    {
        AudioDeviceID input_device = g_input_device == -1 ? ae_get_default_input_device().id : g_input_devices[g_input_device].id;
        AudioDeviceID output_device = g_input_device == -1 ? ae_get_default_output_device().id : g_input_devices[g_input_device].id;
        ae_open_devices(input_device, output_device);
        update_audio_mode_list();
    }

    void try_start_audio_stream()
    {
        ae_start_stream(false, 512 , g_input_modes[g_input_audio_mode].first, g_output_modes[g_output_audio_mode].first, &g_engine);
    }

    void render_settings_ui()
    {
        if (!ImGui::Begin("Settings", &g_settings_window_open, ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            g_should_scan_audio_device = true;
            return;
        }

        if (ImGui::BeginTabBar("settings_tab")) {
            if (ImGui::BeginTabItem("General")) {
                ImGui::Text("Test");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio I/O")) {
                static const char* audio_io_types[] = {
                    "WASAPI",
                };

                int new_audio_driver_type = g_audio_driver_type;
                bool audio_driver_type_changed = false;
                if (ImGui::Combo("Type", &new_audio_driver_type, audio_io_types, 1)) {
                    audio_driver_type_changed = true;
                    ae_close_driver();
                    ae_open_driver((AudioDriverType)new_audio_driver_type);
                }

                if (audio_driver_type_changed || g_should_scan_audio_device) {
                    update_audio_device_list();
                    g_should_scan_audio_device = false;
                }

                // Reset audio device
                if (audio_driver_type_changed) {
                    g_output_device = 0;
                    g_input_device = 0;
                    apply_audio_devices();
                    set_audio_mode_to_default();
                }

                auto device_getter = [](void* data, int idx, const char** out_text) {
                    std::vector<AudioDeviceProperties>* devices = (std::vector<AudioDeviceProperties>*)data;
                    *out_text = devices->at(idx).name;
                    return true;
                };

                auto mode_getter = [](void* data, int idx, const char** out_text) {
                    AudioModeString* endpoints = (AudioModeString*)data;
                    *out_text = endpoints->at(idx).second;
                    return true;
                };

                ImGui::SeparatorText("Output");

                bool audio_device_changed = false;
                ImGui::BeginDisabled(g_output_devices.size() == 1);
                audio_device_changed |=
                    ImGui::Combo("Device##output",
                                 &g_output_device,
                                 device_getter, &g_output_devices,
                                 (int)g_output_devices.size());
                ImGui::EndDisabled();

                audio_device_changed |=
                    ImGui::Combo("Mode##output",
                                 &g_output_audio_mode,
                                 mode_getter, &g_output_modes,
                                 (int)g_output_modes.size());

                ImGui::SeparatorText("Input");

                ImGui::BeginDisabled(g_input_devices.size() == 1);
                audio_device_changed |=
                    ImGui::Combo("Device##input",
                                 &g_input_device,
                                 device_getter, &g_input_devices,
                                 (int)g_input_devices.size());
                ImGui::EndDisabled();

                audio_device_changed |=
                    ImGui::Combo("Mode##input",
                                 &g_input_audio_mode,
                                 mode_getter, &g_input_modes,
                                 (int)g_input_modes.size());

                if (audio_device_changed) {
                    ae_close_devices();
                    apply_audio_devices();
                }

                g_audio_driver_type = new_audio_driver_type;

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Processing")) {
                const char* resampler_modes[] = {
                    "Nearest-neighbor",
                    "Linear",
                    "Cubic",
                    "Catmull-Rom",
                    "Hermite",
                    "Sinc",
                };
                if (ImGui::Combo("Default Resampler", &g_default_resampler_mode, resampler_modes, WB_ARRAY_SIZE(resampler_modes))) {
                    // Resets additional resampler parameter to default value
                    g_default_resampler_mode = 0;
                }

                // Additional resampler param
                if (g_default_resampler_mode == 5) {
                    const char* quality[] = {
                        "8-point",
                        "16-point",
                        "24-point",
                        "32-point",
                        "64-point",
                        "128-point",
                        "256-point",
                        "512-point",
                    };
                    ImGui::Combo("Default Resampler", &g_default_resampler_param, quality, WB_ARRAY_SIZE(quality));
                }
                //ImGui::Combo("Quality")
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    
}
