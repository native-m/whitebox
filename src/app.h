#pragma once

#include "core/common.h"

namespace wb {
struct AppEvent {
    static uint32_t audio_device_removed_event;
};

extern void app_init();
extern void app_run_loop();
extern void app_push_event(uint32_t event_type, void* data, size_t size);
extern void app_shutdown();

extern void set_mouse_cursor_pos(float x, float y);
} // namespace wb