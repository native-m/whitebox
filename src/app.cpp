#include "app.h"
#include "core/debug.h"
#include "core/thread.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "renderer.h"
#include "settings_data.h"
#include "ui/browser.h"
#include "ui/controls.h"
#include "ui/file_dialog.h"
#include "ui/file_dropper.h"
#include "ui/mixer.h"
#include "ui/settings.h"
#include "ui/timeline.h"
#include <imgui.h>
#include <imgui_freetype.h>

using namespace std::literals::chrono_literals;

namespace wb {

void apply_theme(ImGuiStyle& style);

App::~App() {
}

void App::init() {
    Log::info("Initializing UI...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImFontConfig config;
    config.SizePixels = 13.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = false;

    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->AddFontFromFileTTF("assets/Inter-Medium.otf", 0.0f, &config);
    apply_theme(ImGui::GetStyle());

    g_settings_data.load_settings_data();
    init_renderer(this);
    init_audio_io(AudioIOType::WASAPI);
    g_engine.set_bpm(150.0f);
    g_timeline.init();

    g_audio_io->open_device(g_settings_data.output_device_properties.id,
                            g_settings_data.input_device_properties.id);
    g_audio_io->start(&g_engine, g_settings_data.audio_exclusive_mode,
                      g_settings_data.audio_buffer_size, g_settings_data.audio_input_format,
                      g_settings_data.audio_output_format, g_settings_data.audio_sample_rate,
                      AudioThreadPriority::Normal);

    Track* track = g_timeline.add_track();
    g_engine.add_audio_clip_from_file(
        track, "D:/packs/Some samples/NewPacks/Buildups Drum/NM Buildup Drum 09 150 BPM.wav", 0.5);

    // Track* track = g_engine.add_track("New Track");
    // track->add_audio_clip("Clip 1", 4.0, 8.0);
    // track->add_audio_clip("Clip 2", 0.0, 4.0);
}

void App::run() {
    while (running) {
        new_frame();

        if (!g_file_drop.empty()) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
                ImGui::EndDragDropSource();
            }
        }

        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                ImGui::MenuItem("New");
                ImGui::MenuItem("Open...", "Ctrl+O");
                ImGui::MenuItem("Open Recent");
                ImGui::Separator();
                ImGui::MenuItem("Save", "Ctrl+S");
                if (ImGui::MenuItem("Save As...", "Shift+Ctrl+S")) {
                    if (auto file = save_file_dialog({{"Whitebox Project File", "wb"}})) {
                        ProjectFile project_file;
                        g_audio_io->close_device();
                        if (project_file.open(file.value().string(), true)) {
                            project_file.write_project(g_engine, g_sample_table);
                        }
                        g_audio_io->open_device(g_settings_data.output_device_properties.id,
                                                g_settings_data.input_device_properties.id);
                        g_audio_io->start(
                            &g_engine, g_settings_data.audio_exclusive_mode,
                            g_settings_data.audio_buffer_size, g_settings_data.audio_input_format,
                            g_settings_data.audio_output_format, g_settings_data.audio_sample_rate,
                            AudioThreadPriority::Normal);
                    }
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

            ImGui::EndMainMenuBar();
        }

        ImGui::ShowDemoWindow();

        static float tempo = 150.0;
        bool is_playing = g_engine.is_playing();
        ImGui::Begin("Player");
        if (ImGui::DragFloat("Tempo", &tempo, 1.0f, 0.0f, 0.0f, "%.2f BPM")) {
            g_engine.set_bpm((double)tempo);
        }

        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space)) {
            if (is_playing) {
                g_engine.stop();
            } else {
                g_engine.play();
            }
        }
        if (is_playing)
            ImGui::Text("Playing");
        float playhead_pos = g_engine.playhead_ui.load(std::memory_order_relaxed);
        ImGui::Text("Playhead: %f", playhead_pos);
        ImGui::End();

        controls::render_test_controls();

        g_settings.render();
        g_browser.render();
        g_mixer.render();
        g_timeline.render();

        ImGui::Render();

        g_renderer->begin_draw(nullptr, {0.0f, 0.0f, 0.0f, 1.0f});
        g_renderer->render_draw_data(ImGui::GetDrawData());
        g_renderer->finish_draw();
        g_renderer->end_frame();
        g_renderer->present();

        // ImGui::UpdatePlatformWindows();
        // ImGui::RenderPlatformWindowsDefault();
    }
}

void App::shutdown() {
    Log::info("Closing application...");
    g_timeline.shutdown();
    shutdown_audio_io();
    g_sample_table.shutdown();
    shutdown_renderer();
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
    style.FrameRounding = 0.0f;
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
    colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.47f, 0.78f, 1.00f);
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