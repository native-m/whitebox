#include "controls.h"

namespace wb::controls {
void render_test_controls() {
    if (!g_test_control_shown)
        return;

    if (!begin_window("Test Controls", &g_test_control_shown)) {
        end_window();
        return;
    }

    static float value = 0.2f;
    ImU32 grab_color = ImGui::GetColorU32(ImGuiCol_TabActive);
    const KnobProperties knob_props {
        .body_color = 0xFF444444,
        .arc_color = 0xFFED961C,
        .arc_bg_color = 0xFF333333,
        .pointer_color = 0xFFAAAAAA,
        .body_size = 0.8f,
        .pointer_min_len = 0.4f,
        .pointer_max_len = 0.9f,
        .min_angle = std::numbers::pi_v<float> / 6.0f,
        .max_angle = std::numbers::pi_v<float> * 11.0f / 6.0f,
    };

    ImGui::Button("Test");

    ImGui::SeparatorText("Knob");
    controls::knob(knob_props, "##knob_test", ImVec2(100.0f, 100.0f), &value);

    /*
    controls::slider2<float>(
        SliderProperties {
            .grab_shape = controls::SliderGrabShape::Rectangle,
            .grab_size = {16.0f, 28.0f},
            .grab_roundness = 2.0f,
            .extra_padding = {0.0f, 4.0f},
            .frame_width = 4.0f,
        },
        "##slider_test", ImVec2(20.f, 130.0f), 0xFFED961C, &slider_value, 0.0f, 1.0f);

    ImGui::SameLine();

    controls::slider2<float>(
        SliderProperties {
            .grab_shape = controls::SliderGrabShape::Circle,
            .grab_size = {16.0f, 28.0f},
            .grab_roundness = 2.0f,
            .extra_padding = {0.0f, 4.0f},
            .frame_width = 4.0f,
        },
        "##slider_test2", ImVec2(20.f, 130.0f), 0xFFED961C, &slider_value, 0.0f, 1.0f);
        */
    end_window();
}

bool g_test_control_shown = true;
} // namespace wb::controls