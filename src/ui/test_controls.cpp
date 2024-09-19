#include "controls.h"

namespace wb::controls {
void render_test_controls() {
    if (!g_test_control_shown)
        return;

    if (!begin_dockable_window("Test Controls", &g_test_control_shown)) {
        ImGui::End();
        return;
    }

    static float slider_value = 0.0f;
    ImU32 grab_color = ImGui::GetColorU32(ImGuiCol_TabActive);

    ImGui::Button("Test");

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
            .frame_width = 4.0f,
            .extra_padding = {0.0f, 4.0f},
        },
        "##slider_test2", ImVec2(20.f, 130.0f), 0xFFED961C, &slider_value, 0.0f, 1.0f);

    ImGui::End();
}

bool g_test_control_shown = true;
} // namespace wb::controls