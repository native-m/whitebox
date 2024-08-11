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
    int32_t old_resize_height = 0;
    int32_t old_resize_width = 0;

    ~AppSDL2();
    void init() override;
    void process_events() override;
    void new_frame() override;
    void add_vst3_view(VST3Host& plug_instance, const char* name, uint32_t width,
                       uint32_t height) override;
    void handle_events(SDL_Event& event);
    void wait_until_restored();

    static int event_watcher(void* userdata, SDL_Event* event);
};

} // namespace wb