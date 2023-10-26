#pragma once

namespace wb
{
    struct GUIMixer
    {
        bool shown = true;

        void render();
    };

    extern GUIMixer g_gui_mixer;
}