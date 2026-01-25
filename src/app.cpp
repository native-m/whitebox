#include "app.h"

#include <SDL3/SDL_events.h>
// #include <SDL_mouse.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>

#include "app_event.h"
#include "config.h"
#include "core/debug.h"
#include "core/deferred_job.h"
#include "engine/asset.h"
#include "engine/audio_io.h"
#include "engine/command_manager2.h"
#include "engine/engine.h"
#include "engine/engine2.h"
#include "engine/project.h"
#include "gfx/renderer.h"
#include "path_def.h"
#include "plughost/plugin_manager.h"
#include "ui/command_manager.h"
#include "ui/control_bar.h"
#include "ui/dialogs.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/font.h"
#include "ui/hotkeys.h"
#include "ui/timeline2.h"
#include "ui/window.h"
#include "window_manager.h"

using namespace std::literals::chrono_literals;

namespace wb {

static bool is_running = true;
static bool request_quit = false;
static std::string imgui_ini_filepath;

static void audio_device_format_changed_listener(void* userdata);
static void audio_device_removed_listener(void* userdata, bool restart_audio_device);
static void apply_theme(ImGuiStyle& style);

SDL_AppResult app_init(void** appstate, int argc, char** argv) {
  // Init SDL & create main window
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    Log::error("{}", SDL_GetError());
    std::abort();
  }

  init_app_event();
  init_deferred_job();
  init_window_manager();
  Engine2::initialize();

  // Initialize imgui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  load_settings_data();

  imgui_ini_filepath = path_def::imgui_ini_path.string();
  ImGuiIO& io = ImGui::GetIO();
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigDpiScaleFonts = true;
  io.ConfigDpiScaleViewports = true;
  io.ConfigViewportsNoTaskBarIcon = false;
  io.IniFilename = imgui_ini_filepath.c_str();

  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  ImGuiStyle& style = ImGui::GetStyle();
  apply_theme(style);
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;
  style.FontSizeBase = 13.0f;
  style.CircleTessellationMaxError = 0.15f;

  SDL_Window* main_window = wm_get_main_window();
  ImGui_ImplSDL3_InitForOther(main_window);
  init_renderer(main_window);
  init_font_assets();
  init_windows();

  CommandManager2::initialize(10);
  Engine2::set_bpm(140.0f);
  Engine2::add_audio_device_format_changed_listener(nullptr, audio_device_format_changed_listener);
  Engine2::add_audio_device_removed_listener(nullptr, audio_device_removed_listener);
  if (Engine2::init_audio_io())
    Engine2::start_audio_engine();
  // start_audio_engine();

  return SDL_APP_CONTINUE;
}

SDL_AppResult app_iterate(void* appstate) {
  g_renderer->begin_frame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGui::GetDrawListSharedData()->InitialFringeScale = 1.0f;

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  ImGuiID main_dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, ImGuiDockNodeFlags_PassthruCentralNode);

  if (!g_file_drop.empty()) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
      ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
      ImGui::EndDragDropSource();
    }
  }

  hkey_process();

  bool is_playing = Engine2::is_playing();
  if (hkey_pressed(Hotkey::Play)) {
    if (is_playing) {
      Engine2::stop();
      timeline_redraw_window();
    } else {
      Engine2::play();
    }
  }

  if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
    if (hkey_pressed(Hotkey::Undo)) {
      CommandManager2::undo();
    }
    if (hkey_pressed(Hotkey::Redo)) {
      CommandManager2::redo();
    }
  }

  Engine2::update_audio_visualization(GImGui->IO.Framerate);
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
        save_file_dialog_async("save_project_exit", { { "Whitebox Project File (*.wb)", "wb" } });
        request_quit = false;
        break;
      case ConfirmDialog::No: is_running = false; break;
      case ConfirmDialog::Cancel: request_quit = false; break;
      default: break;
    }
  }

  const std::filesystem::path* save_file_path;
  switch (get_file_dialog_payload("save_project_exit", FileDialogType::SaveFile, &save_file_path)) {
    case FileDialogStatus::Accepted: {
      auto result = write_project_file(*save_file_path);
      if (result != ProjectFileResult::Ok) {
        Log::error("Failed to open project {}", (uint32_t)result);
        assert(false);
      }
      is_running = false;
      break;
    }
    case FileDialogStatus::Cancelled: is_running = false; break;
    default: break;
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
      ImGui::DockBuilderDockWindow("Timeline 2", dock_right);
      ImGui::DockBuilderDockWindow("Mixer", dock_right);

      // Bottom-right dock
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

  file_dialog_cleanup();

  if (!is_running)
    return SDL_APP_SUCCESS;

  return SDL_APP_CONTINUE;
}

SDL_AppResult app_handle_event(void* appstate, SDL_Event* event) {
  if (wm_handle_window_event(event))
    return SDL_APP_CONTINUE;

  ImGuiIO& io = GImGui->IO;
  bool is_main_window = math::in_range_inclusive((SDL_EventType)event->type, SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST)
                            ? event->window.windowID == wm_get_main_window_id()
                            : false;

  switch (event->type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
      ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle((void*)(intptr_t)event->window.windowID);
      if (viewport == NULL)
        return SDL_APP_CONTINUE;
      if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        request_quit = is_main_window;
        viewport->PlatformRequestClose = true;
      } else if (event->type == SDL_EVENT_WINDOW_MOVED) {
        viewport->PlatformRequestMove = false;
      } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        viewport->PlatformRequestResize = false;
      } else if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        if (is_main_window) {
          float w = (float)event->window.data1;
          float h = (float)event->window.data2;
          g_renderer->resize_viewport(ImGui::GetMainViewport(), ImVec2((float)w, (float)h));
        }
      }
      return SDL_APP_CONTINUE;
    }
    case SDL_EVENT_DROP_FILE: Log::debug("Drop file"); break;
    case SDL_EVENT_DROP_BEGIN: Log::debug("Drop begin"); break;
    case SDL_EVENT_DROP_COMPLETE: Log::debug("Drop complete"); break;
    case SDL_EVENT_QUIT: request_quit = true; break;
    default: {
      if (event->type >= SDL_EVENT_USER) {
        if (event->type == AppEvent::file_dialog) {
          file_dialog_handle_event(event->user.data1, event->user.data2);
        } else if (event->type == AppEvent::audio_io_type_changed) {
          if (Engine2::audio_io) {
            Engine2::shutdown_audio_io();
          }
          if (Engine2::init_audio_io()) {
            Engine2::reset_audio_engine_config(false);
            Engine2::start_audio_engine();
          }
        } else if (event->type == AppEvent::audio_settings_changed) {
          if (Engine2::is_audio_engine_running()) {
            Engine2::stop_audio_engine();
            Engine2::start_audio_engine();
          }
        } else if (event->type == AppEvent::audio_device_removed_event) {
          bool reset_audio_device = (bool)event->user.data1;
          Engine2::rescan_audio_device();
          if (reset_audio_device) {
            Engine2::stop_audio_engine();
            Engine2::reset_audio_engine_config(false);
            Engine2::start_audio_engine();
          }
        }
      }
      break;
    }
  }

  ImGui_ImplSDL3_ProcessEvent(event);
  return SDL_APP_CONTINUE;
}

void app_quit(void* appstate, SDL_AppResult result) {
  wm_close_all_plugin_window();
  save_settings_data();
  shutdown_windows();
  g_engine.clear_all();
  g_cmd_manager.reset();
  g_sample_table.shutdown();
  g_midi_table.shutdown();
  CommandManager2::flush();
  Engine2::shutdown();
  AssetManager::shutdown();
  shutdown_renderer();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  shutdown_window_manager();
  shutdown_deferred_job();
  SDL_Quit();
}

void audio_device_format_changed_listener(void* userdata) {
}

void audio_device_removed_listener(void* userdata, bool restart_audio_device) {
  app_event_push(AppEvent::audio_device_removed_event, (void*)restart_audio_device);
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
  style.FramePadding = ImVec2(6.0f, 3.0f);
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
  colors[ImGuiCol_FrameBg] = ImVec4(0.221f, 0.221f, 0.243f, 1.000f);
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
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.286f, 0.286f, 0.314f, 1.000f);
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

}  // namespace wb
