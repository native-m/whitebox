#include "controls.h"

namespace wb::controls
{
void render_test_controls() {
    if (!g_test_control_shown)
        return;

    if (!begin_dockable_window("Test Controls", &g_test_control_shown)) {
        ImGui::End();
        return;
    }

    ImGui::Button("Test");

    ImGui::End();
}

bool g_test_control_shown = true;
}