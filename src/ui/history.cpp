#include "history.h"
#include "command_manager.h"
#include "controls.h"

namespace wb {
void render_history_window() {
    static bool open = true;
    if (!open)
        return;

    if (!controls::begin_window("History", &open)) {
        controls::end_window();
        return;
    }

    if (ImGui::Button("Clear All")) {
        g_cmd_manager.reset();
    }

    ImVec2 space = ImGui::GetContentRegionAvail();

    if (ImGui::BeginListBox("##history_listbox", ImVec2(-FLT_MIN, space.y))) {
        uint32_t start = g_cmd_manager.pos;
        uint32_t end = g_cmd_manager.pos + g_cmd_manager.size;
        for (uint32_t i = start; i < end; i++) {
            HistoryItem& item = g_cmd_manager.items[i % g_cmd_manager.size];
            ImGui::Selectable(item.name.c_str());
        }
        ImGui::EndListBox();
    }

    controls::end_window();
}

void render_asset_window() {
    static bool open = true;
    if (!open)
        return;

    if (!controls::begin_window("Assets", &open)) {
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
            ImGui::Selectable(tmp);
            asset = asset->next_;
            midi_id++;
        }
        ImGui::EndListBox();
    }

    controls::end_window();
}

} // namespace wb