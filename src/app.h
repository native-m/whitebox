#pragma once

#include "core/common.h"
#include <SDL3/SDL_init.h>

namespace wb {
extern SDL_AppResult app_init(void** appstate, int argc, char** argv);
extern SDL_AppResult app_iterate(void* appstate);
extern SDL_AppResult app_handle_event(void* appstate, SDL_Event* event);
extern void app_quit(void* appstate, SDL_AppResult result);
}  // namespace wb