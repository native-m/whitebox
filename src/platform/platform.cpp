#include "platform.h"
#include "core/debug.h"
#include "plughost/plugin_interface.h"
#include <SDL_syswm.h>
#include <SDL_mouse.h>
#include <unordered_map>

#ifdef WB_PLATFORM_WINDOWS
#include <dwmapi.h>
#define DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE 20
#define DWM_ATTRIBUTE_CAPTION_COLOR 35
#endif

namespace wb {
static std::unordered_map<uint32_t, SDL_Window*> plugin_windows;

static std::optional<SDL_Window*> get_plugin_window_from_id(uint32_t window_id) {
    if (plugin_windows.empty())
        return {};
    auto plugin_window = plugin_windows.find(window_id);
    if (plugin_window == plugin_windows.end())
        return {};
    return plugin_window->second;
}

void create_main_window() {
    
}

SDL_SysWMinfo get_window_wm_info(SDL_Window* window) {
    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
    return wm_info;
}

void setup_dark_mode(SDL_Window* window) {
    SDL_SysWMinfo wm_info = get_window_wm_info(window);
    ImU32 title_bar_color = ImColor(0.15f, 0.15f, 0.15f, 1.00f) & 0x00FFFFFF;
#ifdef WB_PLATFORM_WINDOWS
    BOOL dark_mode = true;
    ::DwmSetWindowAttribute(wm_info.info.win.window, DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE, &dark_mode,
                            sizeof(dark_mode));
    ::DwmSetWindowAttribute(wm_info.info.win.window, DWM_ATTRIBUTE_CAPTION_COLOR, &title_bar_color,
                            sizeof(title_bar_color));
#endif
}

// TODO(native-m): Replace with SDL function in SDL 3.0
void make_child_window(SDL_Window* window, SDL_Window* parent_window, bool imgui_window) {
    SDL_SysWMinfo wm_info = get_window_wm_info(window);
    SDL_SysWMinfo parent_wm_info = get_window_wm_info(parent_window);
#ifdef WB_PLATFORM_WINDOWS
    HWND handle = wm_info.info.win.window;
    if (imgui_window) {
        BOOL disable_transition = TRUE;
        DWORD style = ::GetWindowLongPtr(handle, GWL_STYLE);
        style = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
        ::SetWindowLongPtr(handle, GWL_STYLE, (LONG)style);
        ::SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
    }
    ::SetWindowLongPtr(handle, GWLP_HWNDPARENT, (LONG_PTR)parent_wm_info.info.win.window);
#endif
}

// Non-native plugin have to use its own window
void add_foreign_plugin_window(PluginInterface* plugin) {
    uint32_t w, h;
    plugin->get_view_size(&w, &h);
    
    SDL_Window* window = SDL_CreateWindow(plugin->get_name(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    if (!window)
        return;

    if (plugin->attach_window(window) != PluginResult::Ok) {
        Log::debug("Failed to create plugin window");
        SDL_DestroyWindow(window);
        return;
    }

    plugin_windows.emplace(SDL_GetWindowID(window), window);
    SDL_SetWindowData(window, "wplg", (void*)plugin); // Attach PluginInterface to the window
}

void close_plugin_window(PluginInterface* plugin) {
    SDL_Window* window = plugin->window_handle;
    if (window)
        plugin->detach_window();
    SDL_DestroyWindow(window);
    plugin_windows.erase(SDL_GetWindowID(window));
}

bool process_plugin_window_event(SDL_Event* event) {
    if (event->type == SDL_WINDOWEVENT) {
        if (auto window = get_plugin_window_from_id(event->window.windowID)) {
            SDL_Window* window_handle = (SDL_Window*)window.value();
            PluginInterface* plugin = (PluginInterface*)SDL_GetWindowData(window_handle, "wplg");
            switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                    if (plugin)
                        close_plugin_window(plugin);
                    break;
            }
        }
        return true;
    }
    return false;
}

void set_mouse_pos(int x, int y) {
    SDL_WarpMouseGlobal(x, y);
}

void enable_relative_mouse_mode(bool relative_mode) {
    SDL_SetRelativeMouseMode((SDL_bool)relative_mode);
}

void get_relative_mouse_state(int* x, int* y) {
    SDL_GetRelativeMouseState(x, y);
}

void reset_relative_mouse_state() {
    int x, y;
    SDL_GetRelativeMouseState(&x, &y);
}

} // namespace wb
