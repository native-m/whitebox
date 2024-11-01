#include "platform.h"
#include <SDL_syswm.h>
#include <SDL_mouse.h>

#ifdef WB_PLATFORM_WINDOWS
#include <dwmapi.h>
#define DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE 20
#define DWM_ATTRIBUTE_CAPTION_COLOR 35
#endif

namespace wb {

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
