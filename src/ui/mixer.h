#pragma once

namespace wb {
struct GuiMixer {
    bool open = true;

    void render();
};

extern GuiMixer g_mixer;
} // namespace wb