#pragma once

#include "core/common.h"

namespace wb {

struct AppEvent {
  static uint32_t file_dialog;
  static uint32_t audio_settings_changed;
  static uint32_t audio_device_removed_event;
};

void init_app_event();
void app_event_push(uint32_t type, void* data1 = nullptr, void* data2 = nullptr);

}  // namespace wb