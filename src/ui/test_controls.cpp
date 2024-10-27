#include "controls.h"

namespace wb::controls {
void render_test_controls() {
    if (!g_test_control_shown)
        return;

    if (!begin_window("Test Controls", &g_test_control_shown)) {
        end_window();
        return;
    }

    static float slider_value = 0.0f;
    ImU32 grab_color = ImGui::GetColorU32(ImGuiCol_TabActive);

    ImGui::Button("Test");

    end_window();
}

bool g_test_control_shown = true;
} // namespace wb::controls