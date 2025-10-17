#include "window_manager.h"

#ifdef WB_PLATFORM_LINUX

namespace wb {

void init_platform_window_manager() {
}

void shutdown_platform_window_manager() {
}

WindowNativeHandle wm_get_native_window_handle(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  std::string_view current_video_driver(SDL_GetCurrentVideoDriver());
  if (current_video_driver == "x11") {
    Display* xdisplay =
        (Display*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    Window xwindow = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (xdisplay && xwindow) {
      return {
        xwindow,
        xdisplay,
      };
    }
  } else if (current_video_driver == "wayland") {
    struct wl_display* display = (struct wl_display*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    struct wl_surface* surface = (struct wl_surface*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (display && surface) {
      return {
        surface,
        display,
      };
    }
  }
  return {};
}

void wm_enable_taskbar_progress_indicator(bool enable) {
  // TODO(native-m): Replace this with SDL_SetWindowProgressState function
  // SDL_SetWindowProgressState(main_window, enable ? SDL_PROGRESS_STATE_NORMAL : SDL_PROGRESS_STATE_NONE);
}

void wm_set_taskbar_progress_value(float progress) {
  // TODO(native-m): Replace this with SDL function
  // SDL_SetWindowProgressValue(main_window, progress);
}

}  // namespace wb

#endif
