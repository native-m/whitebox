#include "app.h"
#include "config.h"
#include "core/color.h"
#include "core/debug.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "gfx/renderer.h"
#include "platform/platform.h"
#include "plughost/vst3host.h"
#include "ui/IconsMaterialSymbols.h"
#include "ui/browser.h"
#include "ui/command_manager.h"
#include "ui/controls.h"
#include "ui/dialogs.h"
#include "ui/env_editor.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/font.h"
#include "ui/history.h"
#include "ui/mixer.h"
#include "ui/piano_roll.h"
#include "ui/settings.h"
#include "ui/timeline.h"
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
static VST3Host vst3_host;
static std::unordered_map<uint32_t, SDL_Window*> plugin_windows;
uint32_t AppEvent::audio_device_removed_event;

static void apply_theme(ImGuiStyle& style);
static int SDLCALL event_watcher(void* userdata, SDL_Event* event);

static void add_vst3_window(VST3Host& plug_instance, const char* name, uint32_t width, uint32_t height) {
#ifdef WB_PLATFORM_WINDOWS
    // Create plugin window
    SDL_Window* window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
    make_child_window(window, main_window);
    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
    setup_dark_mode(window);

    uint32_t id = SDL_GetWindowID(window);
    plugin_windows.emplace(id, window);
    SDL_SetWindowData(window, "wb_vst3_instance", &plug_instance);

    if (plug_instance.view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        Log::debug("Platform is not supported");
        return;
    }
    if (plug_instance.view->attached(wm_info.info.win.window, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk) {
        Log::debug("Failed to attach UI");
        return;
    }
#endif
}

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
    if (event.type == SDL_WINDOWEVENT && event.window.windowID != main_window_id) {
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
    }

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

    NFD::Init();

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

void app_render_control_bar() {
    bool open_menu = false;
    ImVec2 frame_padding = GImGui->Style.FramePadding;
    ImVec4 btn_color = GImGui->Style.Colors[ImGuiCol_Button];
    ImVec4 frame_bg = GImGui->Style.Colors[ImGuiCol_FrameBg];
    bool is_playing = g_engine.is_playing();
    bool is_recording = g_engine.is_recording();
    bool new_project = false;
    bool open_project = false;
    bool save_project = false;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild("WB_TOOLBAR", ImVec2(), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, color_brighten(btn_color, 0.12f).Value);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, color_brighten(frame_bg, 0.12f).Value);

    set_current_font(FontType::Icon);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 3.0f));
    open_menu = ImGui::Button(ICON_MS_MENU);
    ImGui::SameLine(0.0f, 12.0f);
    new_project = ImGui::Button(ICON_MS_LIBRARY_ADD "##wb_new_project");
    ImGui::SameLine(0.0f, 4.0f);
    open_project = ImGui::Button(ICON_MS_FOLDER_OPEN "##wb_open_project");
    ImGui::SameLine(0.0f, 4.0f);
    save_project = ImGui::Button(ICON_MS_SAVE "##wb_save_project");

    //
    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(ICON_MS_UNDO "##wb_undo")) {
        g_cmd_manager.undo();
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(ICON_MS_REDO "##wb_redo")) {
        g_cmd_manager.redo();
    }

    if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().WantTextInput) {
        g_cmd_manager.undo();
    }

    //
    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(!is_playing ? ICON_MS_PLAY_ARROW "##wb_play" : ICON_MS_PAUSE "##wb_play")) {
        if (is_playing) {
            if (g_engine.recording)
                g_timeline.redraw_screen();
            g_engine.stop();
        } else {
            g_engine.play();
        }
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(ICON_MS_STOP "##wb_stop")) {
        if (g_engine.recording)
            g_timeline.redraw_screen();
        g_engine.stop();
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.951f, 0.322f, 0.322f, 1.000f));
    if (controls::toggle_button(ICON_MS_FIBER_MANUAL_RECORD "##wb_record", &is_recording,
                                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive))) {
        if (!is_recording) {
            g_engine.record();
        } else {
            g_timeline.redraw_screen();
            g_engine.stop_record();
        }
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 8.5f));
    ImGui::PushItemWidth(85.0f);
    ImGui::SameLine(0.0f, 4.0f);
    set_current_font(FontType::MonoMedium);
    controls::song_position();
    set_current_font(FontType::Normal);
    ImGui::SameLine(0.0f, 4.0f);
    float tempo = (float)g_engine.get_bpm();
    if (ImGui::DragFloat("##TEMPO_DRAG", &tempo, 1.0f, 0.0f, 0.0f, "%.2f BPM", ImGuiSliderFlags_Vertical)) {
        g_engine.set_bpm((double)tempo);
    }
    ImGui::PopItemWidth();

    // ImGui::SameLine(0.0f, 12.0f);
    // float playhead_pos = g_engine.playhead_ui.load(std::memory_order_relaxed);
    // ImGui::Text("Playhead: %f", playhead_pos);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (open_menu)
        ImGui::OpenPopup("WB_MAIN_MENU_POPUP");

    if (ImGui::BeginPopup("WB_MAIN_MENU_POPUP")) {
        if (ImGui::BeginMenu("File")) {
            new_project = ImGui::MenuItem("New");
            open_project = ImGui::MenuItem("Open...", "Ctrl+O");
            ImGui::MenuItem("Open recent");
            ImGui::Separator();
            ImGui::MenuItem("Save", "Ctrl+S");
            save_project = ImGui::MenuItem("Save as...", "Shift+Ctrl+S");
            ImGui::Separator();
            ImGui::MenuItem("Project info...", nullptr, &g_show_project_dialog);
            ImGui::Separator();
            if (ImGui::MenuItem("Open VST3 plugin (folder)")) {
#ifdef WB_PLATFORM_WINDOWS
                if (auto folder = pick_folder_dialog()) {
                    if (vst3_host.open_module(folder.value().string())) {
                        if (vst3_host.init_view()) {
                            Steinberg::ViewRect rect;
                            vst3_host.view->getSize(&rect);
                            add_vst3_window(vst3_host, "whitebox plugin host", rect.getWidth(), rect.getHeight());
                        }
                    }
                }
#endif
            }
            if (ImGui::MenuItem("Open VST3 plugin (file)")) {
#ifdef WB_PLATFORM_WINDOWS
                if (auto folder = open_file_dialog({{"VST3 Plugin", "vst3"}})) {
                    if (vst3_host.open_module(folder.value().string())) {
                        if (vst3_host.init_view()) {
                            Steinberg::ViewRect rect;
                            vst3_host.view->getSize(&rect);
                            add_vst3_window(vst3_host, "whitebox plugin host", rect.getWidth(), rect.getHeight());
                        }
                    }
                }
#endif
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
                is_running = false;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo");
            ImGui::MenuItem("Redo");
            ImGui::End();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Windows", nullptr, false, false);
            ImGui::Separator();
            ImGui::MenuItem("Timeline", nullptr, &g_timeline.open);
            ImGui::MenuItem("Mixer", nullptr, &g_mixer.open);
            ImGui::MenuItem("Browser", nullptr, &g_browser.open);
            ImGui::MenuItem("Settings", nullptr, &g_settings.open);
            ImGui::MenuItem("Test controls", nullptr, &controls::g_test_control_shown);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("About...");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (new_project) {
        shutdown_audio_io();
        g_engine.clear_all();
        g_timeline.reset();
        g_timeline.add_track();
        g_timeline.recalculate_timeline_length();
        g_timeline.redraw_screen();
        start_audio_engine();
    } else if (open_project) {
        if (auto file = open_file_dialog({{"Whitebox Project File", "wb"}})) {
            shutdown_audio_io();
            g_engine.clear_all();
            auto result = read_project_file(file.value(), g_engine, g_sample_table, g_midi_table, g_timeline);
            if (result != ProjectFileResult::Ok) {
                Log::error("Failed to open project {}", (uint32_t)result);
                assert(false);
            }
            g_timeline.recalculate_timeline_length();
            g_timeline.redraw_screen();
            start_audio_engine();
        }
    } else if (save_project) {
        if (auto file = save_file_dialog({{"Whitebox Project File", "wb"}})) {
            shutdown_audio_io();
            auto result = write_project_file(file.value(), g_engine, g_sample_table, g_midi_table, g_timeline);
            if (result != ProjectFileResult::Ok) {
                Log::error("Failed to open project {}", (uint32_t)result);
                assert(false);
            }
            start_audio_engine();
        }
    }
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

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    g_engine.update_audio_visualization(GImGui->IO.Framerate);

    ImVec2 frame_padding = GImGui->Style.FramePadding;
    ImVec2 window_padding = GImGui->Style.WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 13.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
    if (ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor();
        app_render_control_bar();
        ImGui::EndMainMenuBar();
    }

    ImGui::ShowDemoWindow();
    controls::render_test_controls();
    render_history_window();
    render_asset_window();
    g_settings.render();
    g_browser.render();
    g_mixer.render();
    g_timeline.render();
    g_piano_roll.render();
    g_env_window.render();

    if (g_show_project_dialog)
        project_info_dialog();

    ImGui::Render();
    g_renderer->begin_draw(nullptr, {0.0f, 0.0f, 0.0f, 1.0f});
    g_renderer->render_imgui_draw_data(ImGui::GetDrawData());
    g_renderer->finish_draw();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    g_renderer->end_frame();
    g_renderer->present();

    if (!g_file_drop.empty()) {
        g_file_drop.clear();
    }
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
    g_engine.clear_all();
    g_cmd_manager.reset();
    g_sample_table.shutdown();
    g_midi_table.shutdown();
    shutdown_renderer();
    NFD::Quit();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (!main_window)
        SDL_DestroyWindow(main_window);
    SDL_Quit();
}

void set_mouse_cursor_pos(float x, float y) {
    SDL_WarpMouseGlobal((int)x, (int)y);
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
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
}

} // namespace wb