#include "app_sdl2.h"
#include "renderer.h"
#include "types.h"
#include "global_state.h"
#include <algorithm>
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

        main_window = SDL_CreateWindow("whitebox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        WB_ASSERT(main_window);
        main_window_id = SDL_GetWindowID(main_window);

        SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
        SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);
        SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

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
        
        if (is_file_dropped())
            flush_dropped_files();

        while (SDL_PollEvent(&event))
            handle_events(event);

        update_continued_event();

        Renderer::instance->new_frame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void AppSDL2::handle_events(SDL_Event& event)
    {
        switch (event.type) {
            case SDL_WINDOWEVENT:
                if (event.window.windowID != main_window_id)
                    break;
                if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                    Renderer::instance->resize_swapchain();
                if (event.window.event == SDL_WINDOWEVENT_CLOSE)
                    running = false;
                break;
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEWHEEL:
            {
                auto& io = ImGui::GetIO();
                //IMGUI_DEBUG_LOG("wheel %.2f %.2f, precise %.2f %.2f\n", (float)event->wheel.x, (float)event->wheel.y, event->wheel.preciseX, event->wheel.preciseY);
#if SDL_VERSION_ATLEAST(2,0,18) // If this fails to compile on Emscripten: update to latest Emscripten!
                mouse_wheel.x += -event.wheel.preciseX * 25.0f * io.DeltaTime;
                mouse_wheel.y += event.wheel.preciseY * 25.0f * io.DeltaTime;
#else
                mouse_wheel.x = -event.wheel.x;
                mouse_wheel.y = event.wheel.y;
#endif
#ifdef __EMSCRIPTEN__
                wheel_x /= 100.0f;
#endif
                //ImGui::GetIO().AddMouseWheelEvent(mouse_wheel.x, mouse_wheel.y);
                return;
            }
            case SDL_DROPFILE:
                g_item_dropped.push_back(event.drop.file);
                SDL_free(event.drop.file);
                break;
            default:
                break;
        }

        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    void AppSDL2::update_continued_event()
    {
        auto& io = ImGui::GetIO();
        auto time = 0.001f;
        if ((std::abs(mouse_wheel.x) > io.DeltaTime) || (std::abs(mouse_wheel.y) > io.DeltaTime)) {
            io.AddMouseWheelEvent(mouse_wheel.x, mouse_wheel.y);
            acc += mouse_wheel.y;
            mouse_wheel.x = mouse_wheel.x - mouse_wheel.x * 25.0f * io.DeltaTime;
            mouse_wheel.y = mouse_wheel.y - mouse_wheel.y * 25.0f * io.DeltaTime;
            return;
        }
        else {
            mouse_wheel.x = 0.0f;
            mouse_wheel.y = 0.0f;
            acc = 0.0f;
        }
    }
}