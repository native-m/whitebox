#include "app.h"
#include <SDL.h>
#include <SDL_main.h>

namespace wb
{
    struct AppSDL2 : public App
    {
        SDL_Window* main_window;
        uint32_t main_window_id;
        ImVec2 mouse_wheel;
        float acc = 0.0f;

        virtual ~AppSDL2();
        void init(int argc, const char* argv[]) override;
        void shutdown() override;
        void new_frame() override;
        void handle_events(SDL_Event& event);
        void update_continued_event();
    };
}