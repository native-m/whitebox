#pragma once

#include "core/common.h"
#include "plughost/vst3host.h"

namespace wb {
struct App {
    bool running = true;
    bool settings_window_shown = false;
    VST3Host vst3_host;

    virtual ~App();
    void run();
    void render_control_bar();
    void shutdown();
    void options_window();
    virtual void init();
    virtual void new_frame() = 0;
    virtual void add_vst3_view(VST3Host& plug_instance, const char* name, uint32_t width,
                               uint32_t height) = 0;
};
} // namespace wb