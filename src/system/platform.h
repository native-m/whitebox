#pragma once

#include <SDL_events.h>
#include <SDL_video.h>

#include <optional>

#include "core/common.h"

struct ImGuiViewport;

namespace wb {

struct PluginInterface;

enum class MouseCursor {
  // ImGui cursor
  None = -1,
  Arrow = 0,
  TextInput,
  ResizeAll,
  ResizeNS,
  ResizeEW,
  ResizeNESW,
  ResizeNWSE,
  Hand,
  Wait,
  Progress,
  NotAllowed,

  // Whitebox cursor
  Pencil = 11,
  Pencil2,
  Slice,
};

void init_platform();

// Window
bool wm_create_main_window();
void wm_destroy_main_window();
SDL_Window* wm_get_main_window();
uint32_t wm_get_main_window_id();
void* wm_get_native_window_handle(SDL_Window* window);
void wm_setup_dark_mode(SDL_Window* window);
void wm_set_parent_window(SDL_Window* window, SDL_Window* parent_window, ImGuiViewport* imgui_window = nullptr);
void wm_add_foreign_plugin_window(PluginInterface* plugin);
void wm_close_plugin_window(PluginInterface* plugin);
void wm_close_all_plugin_window();
bool wm_process_plugin_window_event(SDL_Event* event);

// Mouse
void wm_set_mouse_pos(int x, int y);
void wm_enable_relative_mouse_mode(bool relative_mode);
void wm_get_relative_mouse_state(int* x, int* y);
void wm_reset_relative_mouse_state();
void init_mouse_cursor();
void shutdown_mouse_cursor();
void mouse_set_cursor(uint32_t icon_type);

};  // namespace wb