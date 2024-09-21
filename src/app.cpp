#include "app.h"
#include "core/color.h"
#include "core/debug.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "gfx/renderer.h"
#include "gfx/vsync_provider.h"
#include "settings_data.h"
#include "ui/IconsMaterialSymbols.h"
#include "ui/browser.h"
#include "ui/command_manager.h"
#include "ui/controls.h"
#include "ui/env_editor.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/font.h"
#include "ui/history.h"
#include "ui/mixer.h"
#include "ui/piano_roll.h"
#include "ui/settings.h"
#include "ui/timeline.h"
#include <imgui.h>

using namespace std::literals::chrono_literals;

namespace wb {

void apply_theme(ImGuiStyle& style);

App::~App() {
}

void App::init() {
    Log::info("Initializing UI...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_settings_data.load_settings_data();
    g_settings_data.apply_audio_settings();

    NFD::Init();

    ImGuiIO& io = ImGui::GetIO();
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = false;
    io.IniFilename = ".whitebox/ui.ini";

    init_font_assets();
    apply_theme(ImGui::GetStyle());
    init_renderer(this);

    g_cmd_manager.init(10);
    g_timeline.init();
    g_engine.set_bpm(150.0f);
}

void App::run() {
    DrawCommandList cmd_list;
    cmd_list.set_color(ImColor(1.0f, 0.0f, 0.0f, 1.0f));
    cmd_list.draw_rect_filled(ImRect(200.5f, 100.5f, 300.5f, 150.5f));
    cmd_list.draw_triangle_filled(ImVec2(50.0f, 50.0f), ImVec2(90.0f, 40.0f),
                                  ImVec2(100.0f, 70.0f));
    cmd_list.set_color(ImColor(1.0f, 0.0f, 1.0f, 1.0f));
    cmd_list.draw_triangle_filled(ImVec2(50.0f, 50.0f) + ImVec2(10.0f, 0.0f),
                                  ImVec2(90.0f, 40.0f) + ImVec2(10.0f, 0.0f),
                                  ImVec2(100.0f, 70.0f) + ImVec2(10.0f, 0.0f));

    while (running) {
        process_events();
        render();
    }
}

void App::render() {
    new_frame();

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
        } else {
            g_engine.play();
        }
    }

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

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
        render_control_bar();
        ImGui::EndMainMenuBar();
    }

    ImGui::ShowDemoWindow();
    controls::render_test_controls();
    render_history_window();

    float framerate = GImGui->IO.Framerate;
    // Update vu meters
    for (auto track : g_engine.tracks) {
        for (auto& vu_channel : track->level_meter) {
            vu_channel.update(framerate);
        }
    }

    g_settings.render();
    g_browser.render();
    g_mixer.render();
    g_timeline.render();
    g_piano_roll.render();
    g_env_window.render();

    ImGui::Render();
    g_renderer->begin_draw(nullptr, {0.0f, 0.0f, 0.0f, 1.0f});
    g_renderer->render_imgui_draw_data(ImGui::GetDrawData());
    g_renderer->finish_draw();
    g_renderer->end_frame();
    g_renderer->present();
    // ImGui::UpdatePlatformWindows();
    // ImGui::RenderPlatformWindowsDefault();

    if (!g_file_drop.empty()) {
        g_file_drop.clear();
    }
}

void App::render_control_bar() {
    bool open_menu = false;
    ImVec2 frame_padding = GImGui->Style.FramePadding;
    ImVec4 btn_color = GImGui->Style.Colors[ImGuiCol_Button];
    ImVec4 frame_bg = GImGui->Style.Colors[ImGuiCol_FrameBg];
    bool is_playing = g_engine.is_playing();
    bool new_project = false;
    bool open_project = false;
    bool save_project = false;
    static float tempo = 150.0;

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

    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(ICON_MS_UNDO "##wb_undo")) {
        g_cmd_manager.undo();
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(ICON_MS_REDO "##wb_redo")) {
        g_cmd_manager.redo();
    }

    if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z) &&
        !ImGui::GetIO().WantTextInput) {
        g_cmd_manager.undo();
    }

    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(!is_playing ? ICON_MS_PLAY_ARROW "##wb_play" : ICON_MS_PAUSE "##wb_play")) {
        if (is_playing) {
            g_engine.stop();
        } else {
            g_engine.play();
        }
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(ICON_MS_STOP "##wb_stop")) {
        g_engine.stop();
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.951f, 0.322f, 0.322f, 1.000f));
    ImGui::Button(ICON_MS_FIBER_MANUAL_RECORD "##wb_record");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 8.5f));
    ImGui::PushItemWidth(85.0f);
    ImGui::SameLine(0.0f, 4.0f);
    set_current_font(FontType::MonoMedium);
    controls::song_position();
    set_current_font(FontType::Nornal);
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::DragFloat("Tempo", &tempo, 1.0f, 0.0f, 0.0f, "%.2f BPM",
                         ImGuiSliderFlags_Vertical)) {
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
            ImGui::MenuItem("Open Recent");
            ImGui::Separator();
            ImGui::MenuItem("Save", "Ctrl+S");
            save_project = ImGui::MenuItem("Save As...", "Shift+Ctrl+S");
            ImGui::Separator();
            if (ImGui::MenuItem("Open VST3 plugin")) {
#ifdef WB_PLATFORM_WINDOWS
                if (auto folder = pick_folder_dialog()) {
                    if (vst3_host.open_module(folder.value().string())) {
                        if (vst3_host.init_view()) {
                            Steinberg::ViewRect rect;
                            vst3_host.view->getSize(&rect);
                            add_vst3_view(vst3_host, "whitebox plugin host", rect.getWidth(),
                                          rect.getHeight());
                        }
                    }
                }
#endif
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
                running = false;
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
            ImGui::MenuItem("Test Controls", nullptr, &controls::g_test_control_shown);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("About...");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (new_project) {
        g_audio_io->close_device();
        g_engine.clear_all();
        g_timeline.reset();
        g_timeline.add_track();
        g_timeline.recalculate_song_length();
        g_timeline.redraw_screen();
        g_audio_io->open_device(g_settings_data.output_device_properties.id,
                                g_settings_data.input_device_properties.id);
        g_audio_io->start(&g_engine, g_settings_data.audio_exclusive_mode,
                          g_settings_data.audio_buffer_size, g_settings_data.audio_input_format,
                          g_settings_data.audio_output_format, g_settings_data.audio_sample_rate,
                          AudioThreadPriority::Normal);
    } else if (open_project) {
        if (auto file = open_file_dialog({{"Whitebox Project File", "wb"}})) {
            g_audio_io->close_device();
            g_engine.clear_all();
            auto result = read_project_file(file.value(), g_engine, g_sample_table);
            if (result != ProjectFileResult::Ok) {
                Log::error("Failed to open project {}", (uint32_t)result);
                assert(false);
            }
            g_timeline.recalculate_song_length();
            g_timeline.redraw_screen();
            g_audio_io->open_device(g_settings_data.output_device_properties.id,
                                    g_settings_data.input_device_properties.id);
            g_audio_io->start(&g_engine, g_settings_data.audio_exclusive_mode,
                              g_settings_data.audio_buffer_size, g_settings_data.audio_input_format,
                              g_settings_data.audio_output_format,
                              g_settings_data.audio_sample_rate, AudioThreadPriority::Normal);
        }
    } else if (save_project) {
        if (auto file = save_file_dialog({{"Whitebox Project File", "wb"}})) {
            g_audio_io->close_device();
            auto result = write_project_file(file.value(), g_engine, g_sample_table);
            if (result != ProjectFileResult::Ok) {
                Log::error("Failed to open project {}", (uint32_t)result);
                assert(false);
            }
            g_audio_io->open_device(g_settings_data.output_device_properties.id,
                                    g_settings_data.input_device_properties.id);
            g_audio_io->start(&g_engine, g_settings_data.audio_exclusive_mode,
                              g_settings_data.audio_buffer_size, g_settings_data.audio_input_format,
                              g_settings_data.audio_output_format,
                              g_settings_data.audio_sample_rate, AudioThreadPriority::Normal);
        }
    }
}

void App::shutdown() {
    g_settings_data.save_settings_data();
    Log::info("Closing application...");
    g_timeline.shutdown();
    shutdown_audio_io();
    g_engine.clear_all();
    g_cmd_manager.reset();
    g_sample_table.shutdown();
    g_midi_table.shutdown();
    shutdown_renderer();
    NFD::Quit();
}

void App::options_window() {
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
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
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