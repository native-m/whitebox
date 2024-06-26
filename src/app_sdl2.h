#pragma once

#include "app.h"
#include <SDL.h>
#include <unordered_map>

#ifdef main
#undef main
#endif

namespace wb {

struct AppSDL2 : public App {
    SDL_Window* window {};
    uint32_t window_id {};
    std::unordered_map<uint32_t, SDL_Window*> plugin_windows;

    ~AppSDL2();
    void init() override;
    void new_frame() override;
    void add_vst3_view(VST3Host& plug_instance, const char* name, uint32_t width,
                       uint32_t height) override;
    void handle_events(SDL_Event& event);
    void wait_until_restored();
};

} // namespace wb