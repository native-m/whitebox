#include "app_sdl2.h"
#include "core/debug.h"
#include "renderer.h"
#include "ui/file_dropper.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <imgui_impl_sdl2.h>

namespace wb {

AppSDL2::~AppSDL2() {
    shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (!window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}

void AppSDL2::init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    SDL_Window* new_window =
        SDL_CreateWindow("whitebox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                         SDL_WINDOW_RESIZABLE);

    if (!new_window) {
        SDL_Quit();
        return;
    }

    window_id = SDL_GetWindowID(new_window);
    window = new_window;

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    App::init();
}

void AppSDL2::new_frame() {
    if (!g_file_drop.empty()) {
        g_file_drop.clear();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
        handle_events(event);

    g_renderer->new_frame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void AppSDL2::add_vst3_view(VST3Host& plug_instance, const char* name, uint32_t width,
                            uint32_t height) {
    SDL_Window* window =
        SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
    if (plug_instance.view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultTrue) {
        Log::debug("Platform is not supported");
        return;
    }
    if (plug_instance.view->attached(wm_info.info.win.window, Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultOk) {
        Log::debug("Failed to attach UI");
        return;
    }
    uint32_t id = SDL_GetWindowID(window);
    plugin_windows.emplace(id, window);
    SDL_SetWindowData(window, "wb_vst3_instance", &plug_instance);
}

void AppSDL2::handle_events(SDL_Event& event) {
    if (event.window.windowID != window_id) {
        auto plugin_window = plugin_windows.find(event.window.windowID);
        if (plugin_window != plugin_windows.end()) {
            SDL_Window* window = plugin_window->second;
            VST3Host* plug_instance =
                static_cast<VST3Host*>(SDL_GetWindowData(window, "wb_vst3_instance"));
            switch (event.type) {
                case SDL_WINDOWEVENT: {
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        SDL_DestroyWindow(window);
                        plug_instance->view = nullptr;
                        plugin_windows.erase(plugin_window);
                    }
                    break;
                }
            }
            return;
        }
    }

    switch (event.type) {
        case SDL_WINDOWEVENT: {
            if (event.window.windowID != window_id) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    g_renderer->resize_swapchain();
                    break;
                }
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = false;
                    break;
                }
            }
            break;
        }
        case SDL_DROPFILE:
            g_file_drop.push_back(event.drop.file);
            SDL_free(event.drop.file);
            break;
        case SDL_DROPBEGIN:
            Log::debug("Drop begin");
            break;
        case SDL_DROPCOMPLETE:
            Log::debug("Drop complete");
            break;
        case SDL_QUIT:
            running = false;
            break;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
}

} // namespace wb
