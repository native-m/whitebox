#pragma once

#include "core/common.h"

namespace wb {
struct App {
    bool running = true;
    bool settings_window_shown = false;

    virtual ~App();
    void run();
    void options_window();
    virtual void init();
    virtual void new_frame() = 0;
};
} // namespace wb