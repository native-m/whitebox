#pragma once

#include "core/common.h"

namespace wb {
struct GuiSettings {
    bool open;

    void render();
};

extern GuiSettings g_settings;
} // namespace wb