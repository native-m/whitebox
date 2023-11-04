#pragma once

namespace wb
{
    struct GUIStepSequencer
    {
        bool shown = true;
        void render();
    };

    extern GUIStepSequencer g_gui_step_sequencer;
}