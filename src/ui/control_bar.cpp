#include "config.h"
#include "core/color.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "IconsMaterialSymbols.h"
#include "dialogs.h"
#include "command_manager.h"
#include "controls.h"
#include "file_dialog.h"
#include "font.h"
#include "timeline.h"
#include "window.h"

#include <imgui.h>
#include "control_bar.h"

namespace wb {
void render_control_bar() {
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
        main_control_bar();
        ImGui::EndMainMenuBar();
    }
}

void main_control_bar() {
    bool open_menu = false;
    ImVec2 frame_padding = GImGui->Style.FramePadding;
    ImVec4 btn_color = GImGui->Style.Colors[ImGuiCol_Button];
    ImVec4 frame_bg = GImGui->Style.Colors[ImGuiCol_FrameBg];
    bool is_playing = g_engine.is_playing();
    bool is_recording = g_engine.is_recording();
    bool new_project = false;
    bool open_project = false;
    bool save_project = false;
    bool export_audio = false;

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
            save_project = ImGui::MenuItem("Save as...", "Ctrl+Shift+S");
            if (ImGui::MenuItem("Export...", "Ctrl+R")) {
                export_audio = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("Project info...", nullptr, &g_project_info_window_open);
            /*ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
                is_running = false;*/
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
            ImGui::MenuItem("Timeline", nullptr, &g_timeline_window_open);
            ImGui::MenuItem("Mixer", nullptr, &g_mixer_window_open);
            ImGui::MenuItem("Browser", nullptr, &g_browser_window_open);
            ImGui::MenuItem("Plugins", nullptr, &g_plugins_window_open);
            ImGui::MenuItem("Test controls", nullptr, &controls::g_test_control_shown);
            ImGui::Separator();
            ImGui::MenuItem("Settings", nullptr, &g_settings_window_open);
            ImGui::MenuItem("Plugin manager", nullptr, &g_plugin_mgr_window_open);
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
        g_cmd_manager.reset();
        g_timeline.reset();
        g_timeline.add_track();
        g_timeline.recalculate_timeline_length();
        g_timeline.redraw_screen();
        start_audio_engine();
    } else if (open_project) {
        if (auto file = open_file_dialog({{"Whitebox Project File", "wb"}})) {
            shutdown_audio_io();
            g_engine.clear_all();
            g_cmd_manager.reset();
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
    } else if (export_audio) {
        ImGui::OpenPopup("Export audio", ImGuiPopupFlags_AnyPopup);
    }

    export_audio_dialog();
}
} // namespace wb