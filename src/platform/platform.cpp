#include "platform.h"

#include <SDL_mouse.h>
#include <SDL_syswm.h>
#include <SDL_video.h>

#include <unordered_map>

#include "core/debug.h"
#include "plughost/plugin_interface.h"

#ifdef WB_PLATFORM_WINDOWS
#include <dwmapi.h>
#define DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE 20
#define DWM_ATTRIBUTE_CAPTION_COLOR           35
#endif

namespace wb {
static SDL_Window* main_window;
static uint32_t main_window_id;
static std::unordered_map<uint32_t, SDL_Window*> plugin_windows;

static std::optional<SDL_Window*> get_plugin_window_from_id(uint32_t window_id) {
  if (plugin_windows.empty())
    return {};
  auto plugin_window = plugin_windows.find(window_id);
  if (plugin_window == plugin_windows.end())
    return {};
  return plugin_window->second;
}

static void setup_plugin_window(SDL_Window* window) {
#ifdef WB_PLATFORM_WINDOWS
  HWND hwnd = (HWND)wm_get_native_window_handle(window);
  LONG style = GetWindowLong(hwnd, GWL_STYLE);
  SetWindowLong(hwnd, GWL_STYLE, style & ~WS_MINIMIZEBOX);  // Disable minimize button
#endif
}

void init_platform() {
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
}

bool wm_create_main_window() {
  SDL_Window* window = SDL_CreateWindow(
      "whitebox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
  if (!window)
    return false;
  wm_setup_dark_mode(window);
  SDL_SetWindowMinimumSize(window, 640, 480);
  SDL_ShowWindow(window);
  main_window = window;
  main_window_id = SDL_GetWindowID(window);
  return true;
}

void wm_destroy_main_window() {
  SDL_DestroyWindow(main_window);
}

SDL_Window* wm_get_main_window() {
  return main_window;
}

uint32_t wm_get_main_window_id() {
  return main_window_id;
}

void* wm_get_native_window_handle(SDL_Window* window) {
  SDL_SysWMinfo wm_info{};
  SDL_VERSION(&wm_info.version);
  SDL_GetWindowWMInfo(window, &wm_info);
#ifdef WB_PLATFORM_WINDOWS
  return (void*)wm_info.info.win.window;
#else
  return nullptr;
#endif
}

SDL_SysWMinfo wm_get_window_wm_info(SDL_Window* window) {
  SDL_SysWMinfo wm_info{};
  SDL_VERSION(&wm_info.version);
  SDL_GetWindowWMInfo(window, &wm_info);
  return wm_info;
}

void wm_setup_dark_mode(SDL_Window* window) {
  SDL_SysWMinfo wm_info = wm_get_window_wm_info(window);
  ImU32 title_bar_color = ImColor(0.15f, 0.15f, 0.15f, 1.00f) & 0x00FFFFFF;
#ifdef WB_PLATFORM_WINDOWS
  BOOL dark_mode = true;
  ::DwmSetWindowAttribute(wm_info.info.win.window, DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
  ::DwmSetWindowAttribute(wm_info.info.win.window, DWM_ATTRIBUTE_CAPTION_COLOR, &title_bar_color, sizeof(title_bar_color));
#endif
}

// TODO(native-m): Replace with SDL function in SDL 3.0
void wm_make_child_window(SDL_Window* window, SDL_Window* parent_window, bool imgui_window) {
  SDL_SysWMinfo wm_info = wm_get_window_wm_info(window);
  SDL_SysWMinfo parent_wm_info = wm_get_window_wm_info(parent_window);
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
void wm_add_foreign_plugin_window(PluginInterface* plugin) {
  uint32_t w = 256, h = 256;
  // Try request the view size
  if (plugin->get_view_size(&w, &h) != PluginResult::Ok)
    Log::debug("Failed to get window size");

  SDL_Window* window =
      SDL_CreateWindow(plugin->get_name(), plugin->last_window_x, plugin->last_window_y, w, h, SDL_WINDOW_HIDDEN);
  if (!window)
    return;

  setup_plugin_window(window);
  wm_setup_dark_mode(window);
  wm_make_child_window(window, main_window, false);

  if (plugin->attach_window(window) != PluginResult::Ok) {
    Log::debug("Failed to create plugin window");
    SDL_DestroyWindow(window);
    return;
  }

  plugin_windows.emplace(SDL_GetWindowID(window), window);
  SDL_SetWindowData(window, "wplg", (void*)plugin);  // Attach PluginInterface to the window
  SDL_ShowWindow(window);
}

void wm_close_plugin_window(PluginInterface* plugin) {
  SDL_Window* window = plugin->window_handle;
  if (window) {
    SDL_HideWindow(window);
    plugin->detach_window();
    plugin_windows.erase(SDL_GetWindowID(window));
    SDL_DestroyWindow(window);
  }
}

void wm_close_all_plugin_window() {
  if (plugin_windows.size() == 0)
    return;
  for (auto& [id, window] : plugin_windows) {
    PluginInterface* plugin = (PluginInterface*)SDL_GetWindowData(window, "wplg");
    plugin->detach_window();
    SDL_DestroyWindow(window);
  }
  plugin_windows.clear();
}

bool wm_process_plugin_window_event(SDL_Event* event) {
  if (event->type == SDL_WINDOWEVENT) {
    if (auto window = get_plugin_window_from_id(event->window.windowID)) {
      SDL_Window* window_handle = (SDL_Window*)window.value();
      PluginInterface* plugin = (PluginInterface*)SDL_GetWindowData(window_handle, "wplg");
      switch (event->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          if (plugin)
            wm_close_plugin_window(plugin);
          break;
      }
      return true;
    }
  }
  return false;
}

void wm_set_mouse_pos(int x, int y) {
  SDL_WarpMouseGlobal(x, y);
}

void wm_enable_relative_mouse_mode(bool relative_mode) {
  SDL_SetRelativeMouseMode((SDL_bool)relative_mode);
}

void wm_get_relative_mouse_state(int* x, int* y) {
  SDL_GetRelativeMouseState(x, y);
}

void wm_reset_relative_mouse_state() {
  int x, y;
  SDL_GetRelativeMouseState(&x, &y);
}

}  // namespace wb
