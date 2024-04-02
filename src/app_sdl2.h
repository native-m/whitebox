#pragma once

#include "app.h"
#include <SDL.h>

#ifdef main
#undef main
#endif

namespace wb {

struct AppSDL2 : public App {
    SDL_Window* window {};
    uint32_t window_id {};

    ~AppSDL2();
    void init() override;
    void new_frame() override;
    void handle_events(SDL_Event& event);
};

} // namespace wb