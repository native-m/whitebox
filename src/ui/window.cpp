#pragma once

#include "browser.h"
#include "command_manager.h"
#include "controls.h"
#include "engine/engine.h"
#include "env_editor.h"
#include "mixer.h"
#include "piano_roll.h"
#include "plugin_mgr.h"
#include "plugins.h"
#include "settings.h"
#include "timeline.h"

#include <imgui_stdlib.h>

namespace wb {
bool g_browser_window_open = true;
bool g_plugins_window_open = true;
bool g_history_window_open = true;
bool g_asset_window_open = true;
bool g_mixer_window_open = true;
bool g_piano_roll_window_open = true;
bool g_timeline_window_open = true;
bool g_settings_window_open = false;
bool g_plugin_mgr_window_open = false;
bool g_env_editor_window_open = true;
bool g_project_info_window_open = false;

void project_info_window() {
    ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_Once);
    if (!ImGui::Begin("Project Info", &g_project_info_window_open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    ImGui::InputText("Author", &g_engine.project_info.author);
    ImGui::InputText("Title", &g_engine.project_info.title);
    ImGui::InputText("Genre", &g_engine.project_info.genre);
    auto space = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("Description", &g_engine.project_info.description, ImVec2(0.0f, space.y));
    ImGui::End();
}

void history_window() {
    if (!controls::begin_window("History", &g_history_window_open)) {
        controls::end_window();
        return;
    }

    if (ImGui::Button("Clear All"))
        g_cmd_manager.reset();

    ImVec2 space = ImGui::GetContentRegionAvail();

    if (ImGui::BeginListBox("##history_listbox", ImVec2(-FLT_MIN, space.y))) {
        uint32_t start = g_cmd_manager.pos;
        uint32_t end = g_cmd_manager.pos + g_cmd_manager.size;
        for (uint32_t i = start; i < end; i++) {
            HistoryItem& item = g_cmd_manager.items[i % g_cmd_manager.size];
            ImGui::PushID(i);
            ImGui::Selectable(item.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    controls::end_window();
}

void asset_window() {
    if (!controls::begin_window("Assets", &g_asset_window_open)) {
        controls::end_window();
        return;
    }

    ImVec2 space = ImGui::GetContentRegionAvail();
    uint32_t midi_id = 0;
    if (ImGui::BeginListBox("##midi_listbox", ImVec2(-FLT_MIN, space.y * 0.5f))) {
        auto asset = g_midi_table.allocated_assets.next_;
        while (asset != nullptr) {
            auto midi_asset = static_cast<MidiAsset*>(asset);
            char tmp[256] {};
            fmt::format_to(tmp, "MIDI {:x} Refcount: {}", (uint64_t)midi_asset, midi_asset->ref_count);
            ImGui::PushID(midi_id);
            ImGui::Selectable(tmp);
            ImGui::PopID();
            asset = asset->next_;
            midi_id++;
        }
        ImGui::EndListBox();
    }

    controls::end_window();
}

void render_windows() {
    ImGui::ShowDemoWindow();
    controls::render_test_controls();
    if (g_settings_window_open)
        g_settings.render();
    if (g_plugin_mgr_window_open)
        g_plugin_manager.render();
    if (g_browser_window_open)
        g_browser.render();
    if (g_plugins_window_open)
        g_plugins_window.render();
    if (g_history_window_open)
        history_window();
    if (g_asset_window_open)
        asset_window();
    if (g_mixer_window_open)
        g_mixer.render();
    if (g_timeline_window_open)
        g_timeline.render();
    if (g_piano_roll_window_open)
        g_piano_roll.render();
    if (g_env_editor_window_open)
        g_env_window.render();
    if (g_project_info_window_open)
        project_info_window();
}
} // namespace wb