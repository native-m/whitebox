#include "app_sdl2.h"
#include "renderer.h"
#include "types.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>

namespace wb
{
    AppSDL2::~AppSDL2()
    {
        SDL_DestroyWindow(main_window);
        SDL_Quit();
    }

    void AppSDL2::init(int argc, const char* argv[])
    {
        static constexpr uint32_t window_width = 1280;
        static constexpr uint32_t window_height = 720;

        if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
            WB_ASSERT(false);
        }

        main_window = SDL_CreateWindow("white box", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        WB_ASSERT(main_window);
        main_window_id = SDL_GetWindowID(main_window);

        App::init(argc, argv);
    }

    void AppSDL2::shutdown()
    {
        ImGui_ImplSDL2_Shutdown();
        App::shutdown();
    }

    void AppSDL2::new_frame()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_WINDOWEVENT:
                    if (event.window.windowID != main_window_id) {
                        break;
                    }

                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        Renderer::instance->resize_swapchain();
                    }

                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        running = false;
                    }
                    break;
                case SDL_QUIT:
                    running = false;
                    break;
                default:
                    break;
            }
        }

        Renderer::instance->new_frame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }
}