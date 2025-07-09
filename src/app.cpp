#include "app.h"

#include <SDL3/SDL.h>
// #include <SDL_mouse.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "app_event.h"
#include "config.h"
#include "core/debug.h"
#include "core/deferred_job.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "gfx/renderer.h"
#include "path_def.h"
#include "plughost/plugin_manager.h"
#include "ui/command_manager.h"
#include "ui/control_bar.h"
#include "ui/controls.h"
#include "ui/dialogs.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/font.h"
#include "ui/hotkeys.h"
#include "ui/timeline.h"
#include "ui/window.h"
#include "window_manager.h"

using namespace std::literals::chrono_literals;

namespace wb {

static bool is_running = true;
static bool request_quit = false;
static std::string imgui_ini_filepath;

static void handle_events(SDL_Event& event);
static void wait_until_restored();
static bool SDLCALL event_watcher(void* userdata, SDL_Event* event);
static void apply_theme(ImGuiStyle& style);

void app_init() {
  // Init SDL & create main window
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    Log::error("{}", SDL_GetError());
    std::abort();
  }

  init_app_event();
  init_deferred_job();
  init_window_manager();
  init_file_dialog();

  // Initialize imgui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  load_settings_data();

  imgui_ini_filepath = path_def::imgui_ini_path.string();
  ImGuiIO& io = ImGui::GetIO();
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigViewportsNoTaskBarIcon = false;
  io.IniFilename = imgui_ini_filepath.c_str();
  apply_theme(ImGui::GetStyle());

  SDL_Window* main_window = wm_get_main_window();
  ImGui_ImplSDL3_InitForOther(main_window);
  init_font_assets();
  init_renderer(main_window);
  init_windows();
  start_audio_engine();

  g_cmd_manager.init(10);
  g_engine.set_bpm(150.0f);
}

void app_render() {
  if (request_quit)
    is_running = false;

  g_renderer->begin_frame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  ImGuiID main_dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, ImGuiDockNodeFlags_PassthruCentralNode);

  if (!g_file_drop.empty()) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
      ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
      ImGui::EndDragDropSource();
    }
  }

  hkey_process();

  bool is_playing = g_engine.is_playing();
  if (hkey_pressed(Hotkey::Play)) {
    if (is_playing) {
      g_engine.stop();
      g_timeline.redraw_screen();
    } else {
      g_engine.play();
    }
  }

  if (hkey_pressed(Hotkey::Undo)) {
    g_cmd_manager.undo();
  }

  if (hkey_pressed(Hotkey::Redo)) {
    g_cmd_manager.redo();
  }

  g_engine.update_audio_visualization(GImGui->IO.Framerate);
  render_control_bar();
  render_windows();

  if (request_quit) {
    if (g_cmd_manager.is_modified) {
      ImGui::OpenPopup("Exit whitebox##confirm_exit");
      request_quit = false;
    } else {
      is_running = false;
    }
  }

  if (auto ret = confirm_dialog(
          "Exit whitebox##confirm_exit",
          "You have unsaved changes in your file.\n"
          "If you close the application now, any unsaved work will be lost.\n\n"
          "Save changes to untitled.wb?",
          ConfirmDialog::YesNoCancel)) {
    switch (ret) {
      case ConfirmDialog::Yes:
        if (auto file = save_file_dialog({ { "Whitebox Project File", "wb" } })) {
          shutdown_audio_io();
          auto result = write_project_file(file.value(), g_engine, g_sample_table, g_midi_table, g_timeline);
          if (result != ProjectFileResult::Ok) {
            Log::error("Failed to open project {}", (uint32_t)result);
            assert(false);
          }
        }
        is_running = false;
        break;
      case ConfirmDialog::No: is_running = false; break;
      case ConfirmDialog::Cancel: request_quit = false; break;
      default: break;
    }
  }

  static auto setup_docking = true;
  if (setup_docking) {
    if (!std::filesystem::exists(path_def::imgui_ini_path)) {
      ImGuiID dock_right{};
      auto dock_left = ImGui::DockBuilderSplitNode(main_dockspace_id, ImGuiDir_Left, 0.22f, nullptr, &dock_right);
      auto dock_bottom_right = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.35f, nullptr, nullptr);

      // Left dock
      ImGui::DockBuilderDockWindow("Browser", dock_left);
      ImGui::DockBuilderDockWindow("Plugins", dock_left);
      ImGui::DockBuilderDockWindow("History", dock_left);
      ImGui::DockBuilderDockWindow("Assets", dock_left);

      // Right dock (central node)
      ImGui::DockBuilderDockWindow("Timeline", dock_right);

      // Bottom-right dock
      ImGui::DockBuilderDockWindow("Mixer", dock_bottom_right);
      ImGui::DockBuilderDockWindow("Clip Editor", dock_bottom_right);
      ImGui::DockBuilderDockWindow("Env Editor", dock_bottom_right);
      ImGui::DockBuilderDockWindow("Test Controls", dock_bottom_right);

      ImGui::DockBuilderFinish(main_dockspace_id);
    }
    setup_docking = false;
  }

  ImGui::Render();
  g_renderer->begin_render(g_renderer->main_vp->render_target, { 0.0f, 0.0f, 0.0f, 1.0f });
  g_renderer->render_imgui_draw_data(ImGui::GetDrawData());
  g_renderer->end_render();
  ImGui::UpdatePlatformWindows();
  ImGui::RenderPlatformWindowsDefault();
  g_renderer->end_frame();
  g_renderer->present();
}

void app_run_loop() {
  SDL_Event event{};
  while (is_running) {
    while (SDL_PollEvent(&event)) {
      handle_events(event);
    }
    app_render();
  }
}

void app_shutdown() {
  wm_close_all_plugin_window();
  save_settings_data();
  shutdown_windows();
  shutdown_audio_io();
  g_engine.clear_all();
  g_sample_table.shutdown();
  g_midi_table.shutdown();
  g_cmd_manager.reset();
  shutdown_renderer();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  shutdown_window_manager();
  shutdown_deferred_job();
  SDL_Quit();
}

void handle_events(SDL_Event& event) {
  if (wm_process_plugin_window_event(&event))
    return;

  ImGuiIO& io = GImGui->IO;
  bool is_main_window = math::in_range((SDL_EventType)event.type, SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST)
                            ? event.window.windowID == wm_get_main_window_id()
                            : false;

  switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED: {
      ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle((void*)(intptr_t)event.window.windowID);
      if (viewport == NULL)
        return;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        request_quit = is_main_window;
        viewport->PlatformRequestClose = true;
      }
      if (event.type == SDL_EVENT_WINDOW_MOVED)
        viewport->PlatformRequestMove = false;
      if (event.type == SDL_EVENT_WINDOW_RESIZED)
        viewport->PlatformRequestResize = false;
      return;
    }
    case SDL_EVENT_WINDOW_MINIMIZED:
      if (is_main_window)
        wait_until_restored();
      break;
    case SDL_EVENT_DROP_FILE: g_file_drop.push_back(event.drop.data); break;
    case SDL_EVENT_DROP_BEGIN: Log::debug("Drop begin"); break;
    case SDL_EVENT_DROP_COMPLETE: Log::debug("Drop complete"); break;
    case SDL_EVENT_QUIT: request_quit = true; break;
    default: {
      if (event.type >= SDL_EVENT_USER) {
        if (event.type == AppEvent::audio_device_removed_event || event.type == AppEvent::audio_settings_changed) {
          start_audio_engine();
        }
      }
      break;
    }
  }

  ImGui_ImplSDL3_ProcessEvent(&event);
}

void wait_until_restored() {
  SDL_Event next_event;
  while (SDL_WaitEvent(&next_event)) {
    if (next_event.type == SDL_EVENT_WINDOW_RESTORED) {
      if (next_event.window.windowID == wm_get_main_window_id()) {
        break;
      }
    }
  }
}

void apply_theme(ImGuiStyle& style) {
  // Visual Studio style by MomoDeve from ImThemes
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.699999988079071f;
  style.WindowPadding = ImVec2(8.0f, 8.0f);
  style.WindowRounding = 0.0f;
  style.WindowBorderSize = 1.0f;
  style.WindowMinSize = ImVec2(32.0f, 32.0f);
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_Left;
  style.ChildRounding = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupRounding = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.FramePadding = ImVec2(5.0f, 3.0f);
  style.FrameRounding = 2.0f;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(8.0f, 4.0f);
  style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
  style.CellPadding = ImVec2(4.0f, 2.0f);
  style.IndentSpacing = 21.0f;
  style.ColumnsMinSpacing = 6.0f;
  style.ScrollbarSize = 14.0f;
  style.ScrollbarRounding = 0.0f;
  style.GrabMinSize = 10.0f;
  style.GrabRounding = 0.0f;
  style.TabRounding = 3.099999904632568f;
  style.TabBorderSize = 0.0f;
  style.TabBarOverlineSize = 0.0f;
  style.TabCloseButtonMinWidthUnselected = 0.0f;
  style.ColorButtonPosition = ImGuiDir_Right;
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.31f, 0.31f, 0.31f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.11f, 0.59f, 0.93f, 0.51f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.00f, 0.47f, 0.78f, 0.51f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.32f, 0.33f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.37f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.37f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.224f, 0.224f, 0.249f, 1.000f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_Separator] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.32f, 0.32f, 0.33f, 1.00f);
  colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_TabActive] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  // colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.11f, 0.59f, 0.93f, 1.00f);
  colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
  colors[ImGuiCol_DragDropTarget] = ImVec4(0.928f, 0.622f, 0.226f, 1.000f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.149f, 0.149f, 0.149f, 0.455f);
}

#if 0
static int32_t main_window_x;
static int32_t main_window_y;
static int32_t main_window_width;
static int32_t main_window_height;
static ImVec2 scroll_acc;
static bool is_running = true;
static bool request_quit = false;
static std::unordered_map<uint32_t, SDL_Window*> plugin_windows;
uint32_t AppEvent::audio_device_removed_event;

static void apply_theme(ImGuiStyle& style);
static int SDLCALL event_watcher(void* userdata, SDL_Event* event);

static std::optional<SDL_Window*> get_plugin_window_from_id(uint32_t window_id) {
  if (plugin_windows.empty())
    return {};
  auto plugin_window = plugin_windows.find(window_id);
  if (plugin_window != plugin_windows.end())
    return {};
  return plugin_window->second;
}

static void wait_until_restored() {
  SDL_Event next_event;
  while (SDL_WaitEvent(&next_event)) {
    if (next_event.type == SDL_WINDOWEVENT) {
      if (next_event.window.windowID == wm_get_main_window_id() && next_event.window.event == SDL_WINDOWEVENT_RESTORED) {
        break;
      }
    }
  }
}

static void handle_plugin_events(SDL_Event& event) {
  AppEvent::audio_device_removed_event = SDL_RegisterEvents(1);
}

static void handle_events(SDL_Event& event) {
  if (wm_process_plugin_window_event(&event))
    return;

  ImGuiIO& io = GImGui->IO;

  switch (event.type) {
    case SDL_WINDOWEVENT: {
      if (event.window.windowID == wm_get_main_window_id()) {
        switch (event.window.event) {
          case SDL_WINDOWEVENT_EXPOSED: break;
          case SDL_WINDOWEVENT_RESIZED:
            // g_renderer->resize_viewport();
            break;
          case SDL_WINDOWEVENT_CLOSE: request_quit = true; break;
          case SDL_WINDOWEVENT_MINIMIZED: {
            wait_until_restored();
            break;
          }
        }
      }
      break;
    }
    /*case SDL_MOUSEWHEEL: {
      // FIXME(native-m): This break the mouse scroll
      float wheel_x = event.wheel.preciseX;
      float wheel_y = event.wheel.preciseY;
      scroll_acc.x += wheel_x;
      scroll_acc.y += wheel_y;
      return;
    }*/
    case SDL_DROPFILE:
      g_file_drop.push_back(event.drop.file);
      SDL_free(event.drop.file);
      break;
    case SDL_DROPBEGIN: Log::debug("Drop begin"); break;
    case SDL_DROPCOMPLETE: Log::debug("Drop complete"); break;
    case SDL_QUIT: request_quit = true; break;
    default: {
      if (event.type == AppEvent::audio_device_removed_event) {
        shutdown_audio_io();
        start_audio_engine();
      }
      break;
    }
  }

  ImGui_ImplSDL2_ProcessEvent(&event);
}

static void handle_scroll() {
  ImGuiIO& io = GImGui->IO;
  ImVec2 scroll_speed;
  if (math::abs(scroll_acc.x) > 0.00001f) {
    float delta_time = math::min(io.DeltaTime, (float)tm_ms_to_sec(200.0));
    scroll_speed.x = scroll_acc.x * delta_time * 36.0f;
    scroll_acc.x -= scroll_speed.x;
  } else {
    scroll_acc.x = 0.0f;
  }

  if (math::abs(scroll_acc.y) > 0.00001f) {
    float delta_time = math::min(io.DeltaTime, (float)tm_ms_to_sec(200.0));
    scroll_speed.y = scroll_acc.y * delta_time * 36.0f;
    scroll_acc.y -= scroll_speed.y;
  } else {
    scroll_acc.y = 0.0f;
  }

  if (math::abs(scroll_speed.x) > 0.0f || math::abs(scroll_speed.y) > 0.0f) {
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseWheelEvent(-scroll_speed.x, scroll_speed.y);
  }
}

static void register_events() {
  AppEvent::audio_device_removed_event = SDL_RegisterEvents(1);
}

void app_init() {
  init_platform();
  init_deferred_job();

  // Init SDL & create main window
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
    Log::error("{}", SDL_GetError());
    std::abort();
  }

  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_AddEventWatch(event_watcher, nullptr);
  register_events();

  if (!wm_create_main_window()) {
    Log::error("Cannot create window");
    std::abort();
  }

  init_file_dialog();

  // Init imgui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  load_settings_data();
  start_audio_engine();

  ImGuiIO& io = ImGui::GetIO();
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigViewportsNoTaskBarIcon = false;
  io.IniFilename = ".whitebox/ui.ini";

  init_font_assets();
  apply_theme(ImGui::GetStyle());
  init_renderer(wm_get_main_window());
  init_windows();

  g_cmd_manager.init(10);
  g_engine.set_bpm(150.0f);
}

void app_render() {
  // FIXME(native-m): This breaks the mouse scroll
  // handle_scroll();
  g_renderer->begin_frame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  static auto first_time = true;
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  ImGuiID main_dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, ImGuiDockNodeFlags_PassthruCentralNode);

  // FIXME(native-m): Investigate why this breaks maximized window
  /*if (first_time) {
    ImGui::DockBuilderRemoveNode(main_dockspace_id);
    ImGui::DockBuilderAddNode(main_dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodePos(main_dockspace_id, main_viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(main_dockspace_id, main_viewport->WorkSize);

    auto dock_left = ImGui::DockBuilderSplitNode(main_dockspace_id, ImGuiDir_Left, 0.22f, nullptr, &main_dockspace_id);
    auto dock_right = ImGui::DockBuilderSplitNode(main_dockspace_id, ImGuiDir_Right, 0.78f, nullptr, &main_dockspace_id);
    auto dock_bottom_right = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.3f, nullptr, &dock_right);

    ImGui::DockBuilderDockWindow("Browser", dock_left);
    ImGui::DockBuilderDockWindow("Plugins", dock_left);
    ImGui::DockBuilderDockWindow("History", dock_left);
    ImGui::DockBuilderDockWindow("Assets", dock_left);
    ImGui::DockBuilderDockWindow("Timeline", dock_right);
    ImGui::DockBuilderDockWindow("Piano Roll", dock_right);
    ImGui::DockBuilderDockWindow("Mixer", dock_bottom_right);
    ImGui::DockBuilderDockWindow("Test Controls", dock_bottom_right);
    ImGui::DockBuilderDockWindow("Env Editor", dock_bottom_right);
    ImGui::DockBuilderFinish(main_dockspace_id);
    first_time = false;
  }*/

  if (!g_file_drop.empty()) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
      ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
      ImGui::EndDragDropSource();
    }
  }

  hkey_process();

  bool is_playing = g_engine.is_playing();
  if (hkey_pressed(Hotkey::Play)) {
    if (is_playing) {
      g_engine.stop();
      g_timeline.redraw_screen();
    } else {
      g_engine.play();
    }
  }

  if (hkey_pressed(Hotkey::Undo)) {
    g_cmd_manager.undo();
  }

  if (hkey_pressed(Hotkey::Redo)) {
    g_cmd_manager.redo();
  }

  g_engine.update_audio_visualization(GImGui->IO.Framerate);
  render_control_bar();
  render_windows();

  if (request_quit) {
    if (g_cmd_manager.is_modified) {
      ImGui::OpenPopup("Exit whitebox##confirm_exit");
      request_quit = false;
    } else {
      is_running = false;
    }
  }

  if (auto ret = confirm_dialog(
          "Exit whitebox##confirm_exit",
          "You have unsaved changes in your file.\n"
          "If you close the application now, any unsaved work will be lost.\n\n"
          "Save changes to untitled.wb?",
          ConfirmDialog::YesNoCancel)) {
    switch (ret) {
      case ConfirmDialog::Yes:
        if (auto file = save_file_dialog({ { "Whitebox Project File", "wb" } })) {
          shutdown_audio_io();
          auto result = write_project_file(file.value(), g_engine, g_sample_table, g_midi_table, g_timeline);
          if (result != ProjectFileResult::Ok) {
            Log::error("Failed to open project {}", (uint32_t)result);
            assert(false);
          }
        }
        is_running = false;
        break;
      case ConfirmDialog::No: is_running = false; break;
      case ConfirmDialog::Cancel: request_quit = false; break;
      default: break;
    }
  }

  ImGui::Render();
  g_renderer->begin_render(g_renderer->main_vp->render_target, { 0.0f, 0.0f, 0.0f, 1.0f });
  g_renderer->render_imgui_draw_data(ImGui::GetDrawData());
  g_renderer->end_render();
  ImGui::UpdatePlatformWindows();
  ImGui::RenderPlatformWindowsDefault();
  g_renderer->end_frame();
  g_renderer->present();

  if (!g_file_drop.empty())
    g_file_drop.clear();
}

void app_run_loop() {
  while (is_running) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
      handle_events(event);
    app_render();
    // app_render();
  }
}

void app_push_event(uint32_t type, void* data, size_t size) {
  SDL_Event event;
  event.user = {
    .type = type,
    .timestamp = 0,
    .windowID = 0,
    .data1 = data,
  };
  SDL_PushEvent(&event);
}

void app_shutdown() {
  Log::info("Closing application...");
  wm_close_all_plugin_window();
  save_settings_data();
  shutdown_windows();
  shutdown_audio_io();
  g_cmd_manager.reset();
  g_engine.clear_all();
  g_sample_table.shutdown();
  g_midi_table.shutdown();
  shutdown_renderer();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  wm_destroy_main_window();
  shutdown_deferred_job();
  SDL_Quit();
}

static bool last_resized = false;

int SDLCALL event_watcher(void* userdata, SDL_Event* event) {
  bool refresh = false;
  uint32_t main_window_id = wm_get_main_window_id();
  if (!GImGui)
    return 0;
  /*if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_EXPOSED && main_window_id ==
  event->window.windowID) { app_render();
  }*/
  switch (event->type) {
    case SDL_WINDOWEVENT: {
      int32_t w, h;
      switch (event->window.event) {
        case SDL_WINDOWEVENT_MOVED: {
          if (event->window.windowID == main_window_id) {
            if ((main_window_x != event->window.data1 || main_window_y != event->window.data2) && !last_resized) {
              main_window_x = event->window.data1;
              main_window_y = event->window.data2;
              refresh = true;
            }
          } else {
            bool moving = false;
            if (!last_resized) {
              for (uint32_t i = 0; i < GImGui->Viewports.Size; i++) {
                ImGuiViewport* vp = GImGui->Viewports[i];
                uint32_t window_id = (uint32_t)(uint64_t)vp->PlatformHandle;
                if (window_id == main_window_id)
                  continue;
                SDL_Window* window = SDL_GetWindowFromID(window_id);
                int32_t last_x = (int32_t)vp->Pos.x;
                int32_t last_y = (int32_t)vp->Pos.y;
                if (last_x != event->window.data1 && last_y != event->window.data2)
                  refresh = true;
              }
            }
          }
          last_resized = false;
          break;
        }
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
          if (event->window.windowID == main_window_id) {
            if (main_window_width != event->window.data1 || main_window_height != event->window.data2) {
              main_window_width = event->window.data1;
              main_window_height = event->window.data2;
              g_renderer->resize_viewport(
                  ImGui::GetMainViewport(), ImVec2((float)main_window_width, (float)main_window_height));
              refresh = true;
              last_resized = true;
            }
          } else {
            for (uint32_t i = 0; i < GImGui->Viewports.Size; i++) {
              ImGuiViewport* vp = GImGui->Viewports[i];
              uint32_t window_id = (uint32_t)(uint64_t)vp->PlatformHandle;
              if (window_id == main_window_id || window_id != event->window.windowID)
                continue;
              if (has_bit(vp->Flags, ImGuiViewportFlags_NoDecoration))
                continue;
              SDL_Window* window = SDL_GetWindowFromID(window_id);
              int32_t last_w = (int32_t)vp->Size.x;
              int32_t last_h = (int32_t)vp->Size.y;
              if (last_w != event->window.data1 || last_h != event->window.data2) {
                vp->Size.x = (float)event->window.data1;
                vp->Size.y = (float)event->window.data2;
                vp->PlatformRequestResize = true;
                last_resized = true;
              }
            }
            if (last_resized)
              refresh = true;
          }
          break;
        }
        default: break;
      }
      break;
    }
    default: break;
  }
  if (refresh) {
    app_render();
  }
  return 0;
}

#endif

}  // namespace wb
