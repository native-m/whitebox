#include "gui_step_sequencer.h"
#include "global_state.h"
#include "controls.h"

namespace wb
{
    GUIStepSequencer g_gui_step_sequencer;

    void GUIStepSequencer::render()
    {
        if (!shown) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (!controls::begin_dockable_window("Step Sequencer", &shown)) {
            ImGui::End();
            return;
        }

        ImGui::Text("test");
        ImGui::End();
    }
}