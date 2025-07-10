#include "app_event.h"

#include <SDL3/SDL_events.h>

namespace wb {

uint32_t AppEvent::file_dialog;
uint32_t AppEvent::audio_settings_changed;
uint32_t AppEvent::audio_device_removed_event;

void init_app_event() {
  uint32_t base_event_id = SDL_RegisterEvents(3);
  AppEvent::file_dialog = base_event_id;
  AppEvent::audio_settings_changed = base_event_id + 1;
  AppEvent::audio_device_removed_event = base_event_id + 2;
}

void app_event_push(uint32_t type, void* data1, void* data2) {
  SDL_Event event;
  event.user = {
    .type = type,
    .timestamp = 0,
    .windowID = 0,
    .data1 = data1,
    .data2 = data2,
  };
  SDL_PushEvent(&event);
}

}  // namespace wb