#include "app.h"
#include "config.h"
#include "core/color.h"
#include "core/debug.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "gfx/renderer.h"
#include "platform/platform.h"
#include "plughost/plugin_manager.h"
#include "ui/browser.h"
#include "ui/command_manager.h"
#include "ui/control_bar.h"
#include "ui/dialogs.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/font.h"
#include "ui/timeline.h"
#include "ui/window.h"
#include <SDL.h>
#include <SDL_mouse.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>

using namespace std::literals::chrono_literals;

namespace wb {

static SDL_Window* main_window;
static uint32_t main_window_id;
static int32_t main_window_x;
static int32_t main_window_y;
static int32_t main_window_width;
static int32_t main_window_height;
static SDL_SysWMinfo main_wm_info;
static bool is_running = true;
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
            if (next_event.window.windowID == main_window_id && next_event.window.event == SDL_WINDOWEVENT_RESTORED) {
                break;
            }
        }
    }
}

static void handle_plugin_events(SDL_Event& event) {
    AppEvent::audio_device_removed_event = SDL_RegisterEvents(1);
}

static void handle_events(SDL_Event& event) {
    /*if (event.type == SDL_WINDOWEVENT && event.window.windowID != main_window_id) {
        if (auto plugin_window = get_plugin_window_from_id(event.window.windowID)) {
            SDL_Window* window = plugin_window.value();
            switch (event.type) {
                case SDL_WINDOWEVENT: {
                    VST3Host* plug_instance = static_cast<VST3Host*>(SDL_GetWindowData(window, "wb_vst3_instance"));
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        SDL_DestroyWindow(window);
                        plug_instance->view = nullptr;
                        plugin_windows.erase(event.window.windowID);
                    }
                    break;
                }
            }
            return;
        }
    }*/

    switch (event.type) {
        case SDL_WINDOWEVENT: {
            if (event.window.windowID == main_window_id) {
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_RESIZED:
                        // g_renderer->resize_viewport();
                        break;
                    case SDL_WINDOWEVENT_CLOSE:
                        is_running = false;
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED: {
                        wait_until_restored();
                        break;
                    }
                }
            }
            break;
        }
        case SDL_DROPFILE:
            g_file_drop.push_back(event.drop.file);
            SDL_free(event.drop.file);
            break;
        case SDL_DROPBEGIN:
            Log::debug("Drop begin");
            break;
        case SDL_DROPCOMPLETE:
            Log::debug("Drop complete");
            break;
        case SDL_QUIT:
            is_running = false;
            break;
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

static void register_events() {
    AppEvent::audio_device_removed_event = SDL_RegisterEvents(1);
}

static void imgui_renderer_create_window(ImGuiViewport* viewport) {
    SDL_Window* window = SDL_GetWindowFromID((uint32_t)(uint64_t)viewport->PlatformHandle);
    /*if (!has_bit(viewport->Flags, ImGuiViewportFlags_NoDecoration)) {
        setup_dark_mode(window);
    }*/
    make_child_window(window, main_window, true);
    g_renderer->add_viewport(viewport);
}

static void imgui_renderer_destroy_window(ImGuiViewport* viewport) {
    if (viewport->RendererUserData)
        g_renderer->remove_viewport(viewport);
}

static void imgui_renderer_set_window_size(ImGuiViewport* viewport, ImVec2 size) {
    g_renderer->resize_viewport(viewport, size);
}

static void imgui_renderer_render_window(ImGuiViewport* viewport, void* userdata) {
    Framebuffer* fb = (Framebuffer*)viewport->RendererUserData;
    // Log::info("{} {}", fb->width, fb->height);
    if (!has_bit(viewport->Flags, ImGuiViewportFlags_IsMinimized)) {
        g_renderer->begin_draw((Framebuffer*)viewport->RendererUserData, {0.0f, 0.0f, 0.0f, 1.0f});
        g_renderer->render_imgui_draw_data(viewport->DrawData);
        g_renderer->finish_draw();
    }
}

static void imgui_renderer_swap_buffers(ImGuiViewport* viewport, void* userdata) {
}

void app_init() {
    // Init SDL & create main window
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        Log::debug("{}", SDL_GetError());
        std::abort();
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_AddEventWatch(event_watcher, nullptr);

    register_events();
    SDL_Window* new_window =
        SDL_CreateWindow("whitebox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!new_window) {
        SDL_Quit();
        return;
    }

    main_window_id = SDL_GetWindowID(new_window);
    main_window = new_window;
    SDL_GetWindowSize(new_window, &main_window_width, &main_window_height);
    setup_dark_mode(new_window);
    SDL_VERSION(&main_wm_info.version);
    SDL_GetWindowWMInfo(new_window, &main_wm_info);

    init_file_dialog();

    // Init imgui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    load_settings_data();
    init_plugin_manager();
    start_audio_engine();

    ImGuiIO& io = ImGui::GetIO();
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigViewportsNoTaskBarIcon = false;
    io.IniFilename = ".whitebox/ui.ini";

    init_font_assets();
    apply_theme(ImGui::GetStyle());
    init_renderer(main_window);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = imgui_renderer_create_window;
    platform_io.Renderer_DestroyWindow = imgui_renderer_destroy_window;
    platform_io.Renderer_SetWindowSize = imgui_renderer_set_window_size;
    platform_io.Renderer_RenderWindow = imgui_renderer_render_window;
    platform_io.Renderer_SwapBuffers = imgui_renderer_swap_buffers;

    g_cmd_manager.init(10);
    g_timeline.init();
    g_engine.set_bpm(150.0f);
}

void app_render() {
    g_renderer->new_frame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (!g_file_drop.empty()) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
            ImGui::EndDragDropSource();
        }
    }

    bool is_playing = g_engine.is_playing();
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (is_playing) {
            g_engine.stop();
            g_timeline.redraw_screen();
        } else {
            g_engine.play();
        }
    }

    static auto first_time = true;
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGuiID main_dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    if (first_time) {
        ImGui::DockBuilderRemoveNode(main_dockspace_id);
        ImGui::DockBuilderAddNode(main_dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(main_dockspace_id, main_viewport->WorkSize);

        auto dock_left =
            ImGui::DockBuilderSplitNode(main_dockspace_id, ImGuiDir_Left, 0.22f, nullptr, &main_dockspace_id);
        auto dock_right =
            ImGui::DockBuilderSplitNode(main_dockspace_id, ImGuiDir_Right, 0.78f, nullptr, &main_dockspace_id);
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
    }

    g_engine.update_audio_visualization(GImGui->IO.Framerate);
    render_control_bar();
    render_windows();

    ImGui::Render();
    g_renderer->begin_draw(nullptr, {0.0f, 0.0f, 0.0f, 1.0f});
    g_renderer->render_imgui_draw_data(ImGui::GetDrawData());
    g_renderer->finish_draw();
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
    }
}

void app_push_event(uint32_t type, void* data, size_t size) {
    SDL_Event event;
    event.user = {
        .type = type,
        .timestamp = 0,
        .windowID = 0,
    };
    SDL_PushEvent(&event);
}

void app_shutdown() {
    save_settings_data();
    Log::info("Closing application...");
    g_timeline.shutdown();
    shutdown_audio_io();
    g_cmd_manager.reset();
    g_engine.clear_all();
    g_sample_table.shutdown();
    g_midi_table.shutdown();
    shutdown_renderer();
    shutdown_plugin_manager();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (!main_window)
        SDL_DestroyWindow(main_window);
    SDL_Quit();
}

static bool last_resized = false;

int SDLCALL event_watcher(void* userdata, SDL_Event* event) {
    bool refresh = false;
    switch (event->type) {
        case SDL_WINDOWEVENT: {
            int32_t w, h;
            switch (event->window.event) {
                case SDL_WINDOWEVENT_MOVED: {
                    if (event->window.windowID == main_window_id) {
                        if ((main_window_x != event->window.data1 || main_window_y != event->window.data2) &&
                            !last_resized) {
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
                            g_renderer->resize_viewport(ImGui::GetMainViewport(),
                                                        ImVec2((float)main_window_width, (float)main_window_height));
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
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
    if (refresh)
        app_render();
    return 0;
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
    style.TabMinWidthForCloseButton = 0.0f;
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

} // namespace wb