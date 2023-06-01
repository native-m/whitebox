#include "app.h"
#include "def.h"
#include "app_sdl2.h"
#include "renderer.h"
#include "gui_timeline.h"
#include "gui_content_browser.h"
#include "global_state.h"
#include "core/debug.h"
#include "engine/sample_table.h"
#include <imgui.h>
#include <imgui_freetype.h>
#include <chrono>
#include <random>

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
        config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LoadColor | ImGuiFreeTypeBuilderFlags_LightHinting;
        config.RasterizerMultiply = 1.25f;

        io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
        //io.Fonts->AddFontDefault(&config);
        io.Fonts->AddFontFromFileTTF("../../../assets/Inter-Regular.otf", 0.0f, &config);

        ImGuiStyle& style = ImGui::GetStyle();
        //apply_theme(style);
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        g_engine.set_bpm(180.0f);

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
    }
    
    void App::run()
    {
        while (running) {
            new_frame();

            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    ImGui::MenuItem("New");
                    ImGui::MenuItem("Open...", "Ctrl+O");
                    ImGui::Separator();
                    ImGui::MenuItem("Save", "Ctrl+S");
                    ImGui::MenuItem("Save As...", "Shift+Ctrl+S");
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Edit")) {
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Timeline", nullptr, &g_show_timeline_window);
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
            if (is_playing)
                ImGui::Text("%f", g_engine.get_playhead_position());
            for (auto& track : g_engine.tracks)
                ImGui::Text("%s: %f", track->name.c_str(), track->play_time);
            ImGui::End();

            g_gui_content_browser.render();
            g_gui_timeline.render();
            
            if (g_settings_window_open) {
                render_settings_ui();
            }

            ImGui::Render();
            Renderer::instance->render_draw_data(ImGui::GetDrawData());
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
        auto colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.30f, 0.31f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.03f, 0.52f, 0.71f, 0.40f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.67f, 0.91f, 0.53f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.01f, 0.36f, 0.50f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.22f, 0.67f, 0.91f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.03f, 0.52f, 0.71f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.67f, 0.91f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.25f, 0.30f, 0.31f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.03f, 0.52f, 0.71f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.67f, 0.91f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.25f, 0.30f, 0.31f, 0.64f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.03f, 0.52f, 0.71f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.67f, 0.91f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.22f, 0.67f, 0.91f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.22f, 0.67f, 0.91f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        colors[ImGuiCol_Tab] = ImVec4(0.24f, 0.24f, 0.26f, 0.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.67f, 0.91f, 0.80f);
        colors[ImGuiCol_TabActive] = ImVec4(0.03f, 0.52f, 0.71f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.01f, 0.36f, 0.50f, 1.00f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.03f, 0.52f, 0.71f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.22f, 0.24f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        style.GrabRounding = 2.0f;
        style.FrameRounding = 2.0f;
    }
}
