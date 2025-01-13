#pragma once

#include "core/common.h"
#include <SDL_video.h>
#include <SDL_events.h>
#include <optional>

namespace wb {
struct PluginInterface;
// Window
void setup_dark_mode(SDL_Window* window);
void make_child_window(SDL_Window* window, SDL_Window* parent_window, bool imgui_window = false);
void add_foreign_plugin_window(PluginInterface* plugin);
void close_plugin_window(PluginInterface* plugin);
void close_all_plugin_window();
bool process_plugin_window_event(SDL_Event* event);

// Mouse
void set_mouse_pos(int x, int y);
void enable_relative_mouse_mode(bool relative_mode);
void get_relative_mouse_state(int* x, int* y);
void reset_relative_mouse_state();
}; // namespace wb