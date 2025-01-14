#pragma once

#include "core/common.h"
#include <SDL_video.h>
#include <SDL_events.h>
#include <optional>

namespace wb {
struct PluginInterface;
// Window
bool wm_create_main_window();
void wm_destroy_main_window();
SDL_Window* wm_get_main_window();
uint32_t wm_get_main_window_id();
void wm_setup_dark_mode(SDL_Window* window);
void wm_make_child_window(SDL_Window* window, SDL_Window* parent_window, bool imgui_window = false);
void wm_add_foreign_plugin_window(PluginInterface* plugin);
void wm_close_plugin_window(PluginInterface* plugin);
void wm_close_all_plugin_window();
bool wm_process_plugin_window_event(SDL_Event* event);

// Mouse
void wm_set_mouse_pos(int x, int y);
void wm_enable_relative_mouse_mode(bool relative_mode);
void wm_get_relative_mouse_state(int* x, int* y);
void wm_reset_relative_mouse_state();
}; // namespace wb