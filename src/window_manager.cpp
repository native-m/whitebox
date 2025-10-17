#include "window_manager.h"

#include <SDL3/SDL_mouse.h>
#include <imgui.h>
#include <sys/wait.h>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_video.h"
#include "core/debug.h"

#ifdef WB_PLATFORM_MACOS
#include <SDL3/SDL_metal.h>
#endif

namespace wb {

static SDL_Window* main_window;
static int32_t main_window_x;
static int32_t main_window_y;
static int32_t main_window_width;
static int32_t main_window_height;
static bool last_refreshed = false;
static bool is_fullscreen = false;
static uint32_t last_event = 0;
static std::unordered_map<uint32_t, SDL_Window*> plugin_windows;

static std::optional<SDL_Window*> get_plugin_window_from_id(uint32_t window_id) {
  if (plugin_windows.empty())
    return {};
  auto plugin_window = plugin_windows.find(window_id);
  if (plugin_window == plugin_windows.end())
    return {};
  return plugin_window->second;
}

static void wait_until_restored() {
  SDL_Event next_event;
  while (SDL_WaitEvent(&next_event)) {
    if (next_event.type == SDL_EVENT_WINDOW_RESTORED) {
      if (next_event.window.windowID == wm_get_main_window_id()) {
        break;
      }
    }
  }
}

void init_window_manager() {
  init_platform_window_manager();

  uint32_t window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  main_window = SDL_CreateWindow("whitebox", 1280, 720, window_flags);
  SDL_SetWindowMinimumSize(main_window, 640, 480);
  wm_set_dark_mode(main_window);
}

void shutdown_window_manager() {
  SDL_DestroyWindow(main_window);
}

SDL_Window* wm_get_main_window() {
  return main_window;
}

SDL_Window* wm_get_window_from_viewport(ImGuiViewport* vp) {
  return SDL_GetWindowFromID((uint32_t)(uint64_t)vp->PlatformHandle);
}

uint32_t wm_get_main_window_id() {
  return SDL_GetWindowID(main_window);
}

bool wm_is_fullscreen() {
  return is_fullscreen;
}

void wm_add_foreign_plugin_window(PluginInterface* plugin) {
  uint32_t w = 256, h = 256;
  // Try request the view size
  if (plugin->get_view_size(&w, &h) != PluginResult::Ok)
    Log::debug("Failed to get window size");

  SDL_Window* window = SDL_CreateWindow(plugin->get_name(), w, h, SDL_WINDOW_HIDDEN | SDL_WINDOW_UTILITY);
  if (!window)
    return;

  SDL_SetWindowPosition(window, plugin->last_window_x, plugin->last_window_y);
  SDL_SetWindowParent(window, main_window);
  wm_set_dark_mode(window);

  if (plugin->attach_window(window) != PluginResult::Ok) {
    Log::debug("Failed to create plugin window");
    SDL_DestroyWindow(window);
    return;
  }

  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  SDL_SetPointerProperty(props, "wplg", (void*)plugin);  // associate plugin instance with the window

  plugin_windows.emplace(SDL_GetWindowID(window), window);
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
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    PluginInterface* plugin = (PluginInterface*)SDL_GetPointerProperty(props, "wplg", nullptr);
    plugin->detach_window();
    SDL_DestroyWindow(window);
  }
  plugin_windows.clear();
}

bool wm_handle_window_event(SDL_Event* event) {
  if (event->type == SDL_EVENT_WINDOW_MINIMIZED) {
    if (event->window.windowID == wm_get_main_window_id()) {
      wait_until_restored();
    }
  } else if (event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
    if (event->window.windowID == wm_get_main_window_id()) {
      is_fullscreen = true;
    }
  } else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
    if (event->window.windowID == wm_get_main_window_id()) {
      is_fullscreen = false;
      wm_set_dark_mode(main_window);
    }
  } else if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    if (auto window = get_plugin_window_from_id(event->window.windowID)) {
      SDL_Window* w = (SDL_Window*)window.value();
      SDL_PropertiesID props = SDL_GetWindowProperties(w);
      PluginInterface* plugin = (PluginInterface*)SDL_GetPointerProperty(props, "wplg", nullptr);
      if (plugin)
        wm_close_plugin_window(plugin);
      return true;
    }
  }

  return false;
}

void wm_set_mouse_pos(int x, int y) {
  SDL_WarpMouseGlobal((float)x, (float)y);
}

void wm_enable_relative_mouse_mode(ImGuiViewport* vp, bool relative_mode) {
  SDL_SetWindowRelativeMouseMode(wm_get_window_from_viewport(vp), relative_mode);
}

void wm_get_relative_mouse_state(int* x, int* y) {
  float fx, fy;
  SDL_GetRelativeMouseState(&fx, &fy);
  *x = (int)fx;
  *y = (int)fy;
}

void wm_reset_relative_mouse_state() {
  float fx, fy;
  SDL_GetRelativeMouseState(&fx, &fy);
}

}  // namespace wb
