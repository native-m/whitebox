#pragma once

#include "core/common.h"

namespace wb {
extern void app_init();
extern void app_render();
extern void app_run_loop();
extern void app_push_event(uint32_t event_type, void* data, size_t size);
extern void app_shutdown();
}  // namespace wb