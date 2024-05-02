#include "app.h"
#include "core/debug.h"
#include "core/thread.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "renderer.h"
#include "settings_data.h"
#include "ui/browser.h"
#include "ui/controls.h"
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
    g_audio_io->start(g_settings_data.audio_exclusive_mode, g_settings_data.audio_buffer_size,
                      g_settings_data.audio_input_format, g_settings_data.audio_output_format,
                      g_settings_data.audio_sample_rate, AudioThreadPriority::Normal);

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

        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                ImGui::MenuItem("New");
                ImGui::MenuItem("Open...", "Ctrl+O");
                ImGui::MenuItem("Open Recent");
                ImGui::Separator();
                ImGui::MenuItem("Save", "Ctrl+S");
                ImGui::MenuItem("Save As...", "Shift+Ctrl+S");
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

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5921568870544434f, 0.5921568870544434f, 0.5921568870544434f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] =
        ImVec4(0.105882354080677f, 0.105882354080677f, 0.1098039224743843f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] =
        ImVec4(0.105882354080677f, 0.105882354080677f, 0.1098039224743843f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_Border] =
        ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] =
        ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 0.5107296109199524f);
    style.Colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 0.5107296109199524f);
    style.Colors[ImGuiCol_TitleBg] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] =
        ImVec4(0.321568638086319f, 0.321568638086319f, 0.3333333432674408f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(0.3529411852359772f, 0.3529411852359772f, 0.3725490272045135f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] =
        ImVec4(0.3529411852359772f, 0.3529411852359772f, 0.3725490272045135f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_Button] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_Header] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_Separator] =
        ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] =
        ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] =
        ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripHovered] =
        ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] =
        ImVec4(0.321568638086319f, 0.321568638086319f, 0.3333333432674408f, 1.0f);
    style.Colors[ImGuiCol_Tab] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] =
        ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] =
        ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] =
        ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] =
        ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
    style.Colors[ImGuiCol_TextSelectedBg] =
        ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
    style.Colors[ImGuiCol_NavWindowingDimBg] =
        ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
    style.Colors[ImGuiCol_ModalWindowDimBg] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
}

} // namespace wb