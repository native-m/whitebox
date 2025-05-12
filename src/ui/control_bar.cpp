#include "control_bar.h"

#include <imgui.h>

#include "IconsMaterialSymbols.h"
#include "command_manager.h"
#include "config.h"
#include "controls.h"
#include "core/color.h"
#include "core/mem_info.h"
#include "dialogs.h"
#include "engine/engine.h"
#include "engine/project.h"
#include "file_dialog.h"
#include "font.h"
#include "timeline.h"
#include "window.h"

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

void perf_counter_display() {
  const ImGuiStyle& style = GImGui->Style;
  ImGuiID id = ImGui::GetID("##perf_counter_disp");
  ImVec2 size = ImVec2(95.0f, ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
  ImVec2 pos = ImGui::GetCursorScreenPos();

  const ImRect bb(pos, pos + size);
  ImGui::ItemSize(size, style.FramePadding.y);
  if (!ImGui::ItemAdd(bb, id))
    return;

  static double perf_counter_timeout = 0.0;
  static double cpu_usage = 0.0;
  static uint64_t mem_usage = 0.0;
  if (perf_counter_timeout == 0.0) {
    MemoryInfo mem_info = get_app_memory_info();
    perf_counter_timeout = tm_ms_to_sec(100.0);
    cpu_usage = g_engine.perf_measurer.get_usage() * 100.0;
    mem_usage = mem_info.overall_usage;
  } else {
    perf_counter_timeout = math::max(perf_counter_timeout - GImGui->IO.DeltaTime, 0.0);
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
  dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);

  const char* cpu_usage_begin;
  const char* cpu_usage_end;
  ImFormatStringToTempBuffer(&cpu_usage_begin, &cpu_usage_end, "%.1f%%", cpu_usage);
  ImVec2 cpu_usage_size = ImGui::CalcTextSize(cpu_usage_begin, cpu_usage_end);

  dl->AddText(bb.Min + ImVec2(4.0f, 2.0f), text_col, "CPU");
  dl->AddText(ImVec2(bb.Max.x - cpu_usage_size.x - 4.0f, bb.Min.y + 2.0f), text_col, cpu_usage_begin, cpu_usage_end);

  const char* mem_usage_begin;
  const char* mem_usage_end;
  ImFormatStringToTempBuffer(&mem_usage_begin, &mem_usage_end, "%.1f MB", (double)mem_usage / 1000000.0);
  ImVec2 mem_usage_size = ImGui::CalcTextSize(mem_usage_begin, mem_usage_end);

  dl->AddText(bb.Min + ImVec2(4.0f, cpu_usage_size.y + 2.0f), text_col, "Mem");
  dl->AddText(
      ImVec2(bb.Max.x - mem_usage_size.x - 4.0f, bb.Min.y + cpu_usage_size.y + 2.0f),
      text_col,
      mem_usage_begin,
      mem_usage_end);
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
  ImGui::PushStyleColor(ImGuiCol_Button, Color(btn_color).brighten(0.12f).to_uint32());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(frame_bg).brighten(0.12f).to_uint32());

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 3.0f));
  set_current_font(FontType::Icon);
  open_menu = ImGui::Button(ICON_MS_MENU);
  controls::item_tooltip("Main menu");
  ImGui::SameLine(0.0f, 12.0f);
  new_project = ImGui::Button(ICON_MS_NOTE_ADD "##wb_new_project");
  controls::item_tooltip("Create new project");
  ImGui::SameLine(0.0f, 4.0f);
  open_project = ImGui::Button(ICON_MS_FOLDER_OPEN "##wb_open_project");
  controls::item_tooltip("Open project file");
  ImGui::SameLine(0.0f, 4.0f);
  save_project = ImGui::Button(ICON_MS_SAVE "##wb_save_project");
  controls::item_tooltip("Save project file");
  ImGui::SameLine(0.0f, 12.0f);

  //
  if (ImGui::Button(ICON_MS_UNDO "##wb_undo")) {
    g_cmd_manager.undo();
  }
  controls::item_tooltip("Undo");
  ImGui::SameLine(0.0f, 4.0f);
  
  if (ImGui::Button(ICON_MS_REDO "##wb_redo")) {
    g_cmd_manager.redo();
  }
  controls::item_tooltip("Redo");
  ImGui::SameLine(0.0f, 12.0f);

  // Transport button
  if (ImGui::Button(!is_playing ? ICON_MS_PLAY_ARROW "##wb_play" : ICON_MS_PAUSE "##wb_play")) {
    if (is_playing) {
      if (g_engine.recording)
        g_timeline.redraw_screen();
      g_engine.stop();
    } else {
      g_engine.play();
    }
  }
  controls::item_tooltip("Play or pause");
  ImGui::SameLine(0.0f, 4.0f);
  
  if (ImGui::Button(ICON_MS_STOP "##wb_stop")) {
    if (g_engine.recording)
      g_timeline.redraw_screen();
    g_engine.stop();
  }
  controls::item_tooltip("Stop");
  ImGui::SameLine(0.0f, 4.0f);

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.951f, 0.322f, 0.322f, 1.000f));
  if (controls::toggle_button(
          ICON_MS_FIBER_MANUAL_RECORD "##wb_record", &is_recording, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive))) {
    if (!is_recording) {
      g_engine.record();
    } else {
      g_timeline.redraw_screen();
      g_engine.stop_record();
    }
  }
  ImGui::PopStyleColor();
  controls::item_tooltip("Record");
  ImGui::PopStyleVar();
  ImGui::SameLine(0.0f, 4.0f);

  // Song position
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 3.0f));
  ImGui::PushItemWidth(85.0f);
  set_current_font(FontType::MonoMedium);
  controls::song_position();
  set_current_font(FontType::Normal);
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::PopStyleVar();

  // Tempo
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_padding.x, 8.5f));
  float tempo = (float)g_engine.get_bpm();
  if (ImGui::DragFloat("##tempo_drag", &tempo, 1.0f, 0.0f, 0.0f, "%.2f BPM", ImGuiSliderFlags_Vertical)) {
    g_engine.set_bpm((double)tempo);
  }
  controls::item_tooltip("Tempo (BPM)");
  ImGui::PopItemWidth();

  ImGui::SameLine(0.0f, 12.0f);
  perf_counter_display();

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
    g_cmd_manager.reset(true);
    g_timeline.reset();
    g_timeline.add_track();
    g_timeline.recalculate_song_length();
    g_timeline.redraw_screen();
    start_audio_engine();
  } else if (open_project) {
    if (auto file = open_file_dialog({ { "Whitebox Project File", "wb" } })) {
      shutdown_audio_io();
      g_engine.clear_all();
      g_cmd_manager.reset(true);
      auto result = read_project_file(file.value(), g_engine, g_sample_table, g_midi_table, g_timeline);
      if (result != ProjectFileResult::Ok) {
        Log::error("Failed to open project {}", (uint32_t)result);
        assert(false);
      }
      g_timeline.recalculate_song_length();
      g_timeline.redraw_screen();
      start_audio_engine();
    }
  } else if (save_project) {
    if (auto file = save_file_dialog({ { "Whitebox Project File", "wb" } })) {
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
}  // namespace wb