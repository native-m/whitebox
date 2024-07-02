#include "piano_roll.h"
#include "controls.h"
#include "engine/engine.h"
#include "file_dialog.h"
#include <imgui.h>

namespace wb {

const char* note_scale[] = {
    "C",
    "C#",
    "D",
    "D#",
    "E",
    "F",
    "F#",
    "G",
    "G#",
    "A",
    "A#",
    "B",
};

void GuiPianoRoll::open_midi_file() {
    if (auto file = open_file_dialog({{"Standard MIDI File", "mid"}})) {
        midi_note = load_notes_from_file(file.value());
    }
}
void GuiPianoRoll::render() {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!controls::begin_dockable_window("Piano Roll", &open)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    ImGui::PopStyleVar();

    ImGui::BeginChild("PianoRollControl", ImVec2(100.0f, 0.0f), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_MenuBar);
    if (ImGui::Button("Open")) {
        open_midi_file();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("PianoRoll", ImVec2());
    auto cursor_pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("PianoRollArea",
                           ImVec2(ImGui::GetContentRegionAvail().x, 128.0f * 18.0f));

    static const ImU32 channel_color = ImColor(121, 166, 91);
    auto draw_list = ImGui::GetWindowDrawList();
    auto font = ImGui::GetFont();
    for (auto& note : midi_note) {
        char note_name[5] {};
        const char* scale = note_scale[note.note_number % 12];
        fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, note.note_number / 12);

        float pos_y = (float)(127 - note.note_number) * 18.0f;
        float min_pos_x = cursor_pos.x + (float)math::round(note.min_time * g_engine.ppq * 0.25);
        float max_pos_x = cursor_pos.x + (float)math::round(note.max_time * g_engine.ppq * 0.25);
        ImVec2 min_bb(min_pos_x, cursor_pos.y + pos_y);
        ImVec2 max_bb(max_pos_x, cursor_pos.y + pos_y + 18.0f);
        ImVec4 label(min_bb.x, min_bb.y, max_bb.x - 6.0f, max_bb.y);
        draw_list->AddRectFilled(min_bb, max_bb, channel_color);
        draw_list->AddRect(min_bb, max_bb, 0x7F000000);
        draw_list->AddText(font, font->FontSize,
                           ImVec2(std::max(cursor_pos.x, min_pos_x) + 4.0f, min_bb.y + 2.0f),
                           0xFFFFFFFF, note_name, nullptr, 0.0f, &label);
    }

    ImGui::Button("test");
    ImGui::EndChild();

    ImGui::End();
}

GuiPianoRoll g_piano_roll;
} // namespace wb