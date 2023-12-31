#include "app.h"
#include "def.h"
#include "stdpch.h"
#include "app_sdl2.h"
#include "gui_timeline.h"
#include "gui_content_browser.h"
#include "gui_step_sequencer.h"
#include "gui_mixer.h"
#include "global_state.h"
#include "controls.h"
#include "core/debug.h"
#include "engine/audio_stream.h"
#include "engine/engine.h"
#include "engine/sample_table.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <nfd.hpp>

namespace wb
{
    void apply_theme(ImGuiStyle& style);

    void App::init(int argc, const char* argv[])
    {
        ae_open_driver(AudioDriverType::WASAPI);
        update_audio_device_list();
        apply_audio_devices();
        set_audio_mode_to_default();
        try_start_audio_stream();

        NFD::Init();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        //io.ConfigViewportsNoAutoMerge = false;
        io.ConfigViewportsNoTaskBarIcon = false;

        ImFontConfig config;
        config.SizePixels = 13.0f;
        config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LoadColor;
        config.RasterizerMultiply = 1.0f;
        config.GlyphOffset.x = -1.0f;

        io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
        io.Fonts->AddFontFromFileTTF("../../../assets/Inter-Medium.otf", 0.0f, &config);

        ImGuiStyle& style = ImGui::GetStyle();
        apply_theme(style);

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        /*std::random_device rd;
        std::uniform_int_distribution<uint32_t> dist(1, 9);

        for (uint32_t i = 0; i < 64; i++) {
            auto track = g_engine.add_track(TrackType::Audio, "Audio Track");
            track->color = ImColor::HSV((float)i / 64.f, 0.5f, 0.5f);

            double current_time = 0;

            for (uint32_t j = 0; j < 15; j++) {
                double min_time = current_time + (double)dist(rd);
                double max_time = min_time + (double)dist(rd);
                
                if (max_time > g_gui_timeline.music_length / 96.0f)
                    break;

                current_time = max_time;
                auto clip = track->add_audio_clip(min_time, max_time, nullptr);
                fmt::format_to(std::back_inserter(clip->name), "Test Clip {}", j);
            }
        }*/
        
        Renderer::init(this, Renderer::D3D11);
        g_gui_timeline.initialize();

        g_engine.on_bpm_change = [&]() {
            g_gui_timeline.redraw_clip_content();
        };

        g_engine.set_bpm(150.0f);
    }
    
    void App::run()
    {
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        
        while (running) {
            new_frame();

            if (is_file_dropped()) {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                    ImGui::SetDragDropPayload("ExternalFileDrop", nullptr, 0, ImGuiCond_Once);
                    ImGui::EndDragDropSource();
                }
            }

            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    ImGui::MenuItem("New");
                    ImGui::MenuItem("Open...", "Ctrl+O");
                    ImGui::Separator();
                    ImGui::MenuItem("Save", "Ctrl+S");
                    ImGui::MenuItem("Save As...", "Shift+Ctrl+S");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit"))
                        running = false;
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Edit")) {
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Timeline", nullptr, &g_show_timeline_window);
                    ImGui::MenuItem("Mixer", nullptr, &g_gui_mixer.shown);
                    ImGui::MenuItem("Browser", nullptr, &g_show_content_browser);
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Options")) {
                    ImGui::MenuItem("Settings...", nullptr, &g_settings_window_open);
                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }

            ImGui::ShowDemoWindow();

            ImGui::Begin("Player");
            bool is_playing = g_engine.is_playing();
            if (ImGui::Button(!is_playing ? "Play##play_button" : "Stop##play_button") || ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (is_playing)
                    g_engine.stop();
                else
                    g_engine.play();
            }
            float tempo = (float)(60.0 / g_engine.beat_duration.load(std::memory_order_relaxed));
            if (ImGui::DragFloat("tempo", &tempo, 1.0f, 0.0f, 0.0f, "%.2f"))
                g_engine.set_bpm(tempo);
            if (is_playing)
                ImGui::Text("%f", g_engine.get_playhead_position());
            ImGui::End();

            g_gui_content_browser.render();
            g_gui_timeline.render();
            g_gui_step_sequencer.render();
            g_gui_mixer.render();

            if (g_settings_window_open)
                render_settings_ui();

            ImVec4 bg_col = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);

            ImGui::Render();
            Renderer::instance->set_framebuffer(nullptr);
            Renderer::instance->clear_framebuffer(bg_col.x, bg_col.y, bg_col.z, 1.0f);
            Renderer::instance->render_imgui(ImGui::GetDrawData());
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            Renderer::instance->present();
        }
    }

    void App::shutdown()
    {
        ae_close_driver();
        Renderer::shutdown();
        ImGui::DestroyContext();
        NFD::Quit();
    }

    int App::run(int argc, const char* argv[])
    {
        AppSDL2 app;
        app.init(argc, argv);
        app.run();
        app.shutdown();
        return 0;
    }

    void apply_theme(ImGuiStyle& style)
    {
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
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5921568870544434f, 0.5921568870544434f, 0.5921568870544434f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.105882354080677f, 0.105882354080677f, 0.1098039224743843f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.105882354080677f, 0.105882354080677f, 0.1098039224743843f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 0.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 0.5107296109199524f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 0.5107296109199524f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.321568638086319f, 0.321568638086319f, 0.3333333432674408f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3529411852359772f, 0.3529411852359772f, 0.3725490272045135f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.3529411852359772f, 0.3529411852359772f, 0.3725490272045135f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.321568638086319f, 0.321568638086319f, 0.3333333432674408f, 1.0f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.1137254908680916f, 0.5921568870544434f, 0.9254902005195618f, 1.0f);
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
        style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.4666666686534882f, 0.7843137383460999f, 1.0f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    }
}
