#include "app.h"
#include <SDL.h>
#include <SDL_main.h>

namespace wb
{
    struct AppSDL2 : public App
    {
        SDL_Window* main_window;
        uint32_t main_window_id;

        virtual ~AppSDL2();
        void init(int argc, const char* argv[]) override;
        void shutdown() override;
        void new_frame() override;
    };
}