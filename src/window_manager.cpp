#include "window_manager.h"

#include "app.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include "gfx/renderer.h"

#ifdef WB_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <dwmapi.h>
#define DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE 20
#define DWM_ATTRIBUTE_CAPTION_COLOR           35
#endif

namespace wb {

static SDL_Window* main_window;
static int32_t main_window_x;
static int32_t main_window_y;
static int32_t main_window_width;
static int32_t main_window_height;
static bool last_refreshed = false;
static uint32_t last_event = 0;

static bool SDLCALL event_watcher(void* userdata, SDL_Event* event) {
  bool refresh = false;
  uint32_t main_window_id = wm_get_main_window_id();

  if (!GImGui)
    return false;

  if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
    if (event->window.windowID == main_window_id) {
      int x, y, w, h;
      SDL_GetWindowSize(main_window, &w, &h);
      SDL_GetWindowPosition(main_window, &x, &y);
      if (main_window_width != w || main_window_height != h) {
        g_renderer->resize_viewport(ImGui::GetMainViewport(), ImVec2((float)w, (float)h));
        main_window_width = w;
        main_window_height = h;
      }
      if (main_window_x != x || main_window_y != y) {
        main_window_x = x;
        main_window_y = y;
      }
      app_render();
    }
  }

  return false;
}

void init_window_manager() {
#ifdef WB_PLATFORM_WINDOWS
  enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
  using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
  HMODULE uxtheme = LoadLibraryEx(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (uxtheme) {
    SetPreferredAppModeFn set_preferred_app_mode_fn = (SetPreferredAppModeFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    if (set_preferred_app_mode_fn)
      set_preferred_app_mode_fn(PreferredAppMode::ForceDark);
  }
#endif

  SDL_AddEventWatch(event_watcher, nullptr);

  main_window = SDL_CreateWindow("whitebox", 1280, 720, SDL_WINDOW_RESIZABLE);
  SDL_SetWindowMinimumSize(main_window, 640, 480);
  wm_setup_dark_mode(main_window);
}

void shutdown_window_manager() {
  SDL_DestroyWindow(main_window);
}

bool wm_create_main_window() {
  return true;
}

void wm_destroy_main_window() {
}

SDL_Window* wm_get_main_window() {
  return main_window;
}

uint32_t wm_get_main_window_id() {
  return SDL_GetWindowID(main_window);
}

WindowNativeHandle wm_get_native_window_handle(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
#if defined(WB_PLATFORM_WINDOWS)
  HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  if (hwnd) {
    return {
      hwnd,
      nullptr,
    };
  }
#elif defined(WB_PLATFORM_LINUX)
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
#endif
  return {};
}

void wm_setup_dark_mode(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  ImU32 title_bar_color = ImColor(0.15f, 0.15f, 0.15f, 1.00f) & 0x00FFFFFF;
#ifdef WB_PLATFORM_WINDOWS
  HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  BOOL dark_mode = true;
  ::DwmSetWindowAttribute(hwnd, DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
  ::DwmSetWindowAttribute(hwnd, DWM_ATTRIBUTE_CAPTION_COLOR, &title_bar_color, sizeof(title_bar_color));
#endif
}

void wm_set_parent_window(SDL_Window* window, SDL_Window* parent_window, ImGuiViewport* imgui_window) {
}

void wm_add_foreign_plugin_window(PluginInterface* plugin) {
}

void wm_close_plugin_window(PluginInterface* plugin) {
}

void wm_close_all_plugin_window() {
}

bool wm_process_plugin_window_event(SDL_Event* event) {
  return false;
}

void wm_set_mouse_pos(int x, int y) {
}

void wm_enable_relative_mouse_mode(bool relative_mode) {
}

void wm_get_relative_mouse_state(int* x, int* y) {
}

void wm_reset_relative_mouse_state() {
}

}  // namespace wb