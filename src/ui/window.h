#pragma once

namespace wb {
extern bool g_browser_window_open;
extern bool g_plugins_window_open;
extern bool g_history_window_open;
extern bool g_asset_window_open;
extern bool g_mixer_window_open;
extern bool g_piano_roll_window_open;
extern bool g_timeline_window_open;
extern bool g_settings_window_open;
extern bool g_plugin_mgr_window_open;
extern bool g_env_editor_window_open;
extern bool g_project_info_window_open;

void init_windows();
void shutdown_windows();
void render_windows();

void project_info_window();
void history_window();
void asset_window();

} // namespace wb