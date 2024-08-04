#include "piano_roll.h"
#include "controls.h"
#include "core/color.h"
#include "engine/engine.h"
#include "file_dialog.h"
#include <imgui.h>

namespace wb {

const char* note_scale[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

GuiPianoRoll::GuiPianoRoll() {
    separator_pos = 70.0f;
    min_track_control_size = 70.0f;
}

void GuiPianoRoll::open_midi_file() {
    if (auto file = open_file_dialog({{"Standard MIDI File", "mid"}})) {
        load_notes_from_file(midi_note, file.value());
    }
}
void GuiPianoRoll::render() {
    if (!open)
        return;

    double ppq = g_engine.ppq;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!controls::begin_dockable_window("Piano Roll", &open)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    ImGui::PopStyleVar();

    if (ImGui::BeginChild("PianoRollControl", ImVec2(100.0f, 0.0f), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_MenuBar)) {
        if (ImGui::Button("Open")) {
            open_midi_file();
        }
        ImGui::EndChild();
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
    if (ImGui::BeginChild("PianoRoll", ImVec2())) {
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        render_horizontal_scrollbar();
        double new_time_pos = 0.0;
        if (render_time_ruler(&new_time_pos)) {
        }
        ImGui::PopStyleVar();

        ImVec2 content_origin = ImGui::GetCursorScreenPos();
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImVec2 main_cursor_pos = cursor_pos;
        ImVec2 child_content_size = ImGui::GetContentRegionAvail();
        float view_height = child_content_size.y;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(
            ImVec2(content_origin.x, content_origin.y - 1.0f),
            ImVec2(content_origin.x + ImGui::GetContentRegionAvail().x, content_origin.y - 1.0f),
            ImGui::GetColorU32(ImGuiCol_Separator));

        ImGui::BeginChild("PianoRollChild", ImVec2());
        draw_list = ImGui::GetWindowDrawList();
        vscroll = ImGui::GetScrollY();

        float separator_x = cursor_pos.x + min_track_control_size + 0.5f;
        draw_list->AddLine(ImVec2(separator_x, cursor_pos.y),
                           ImVec2(separator_x, cursor_pos.y + child_content_size.y),
                           ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

        static constexpr float note_count = 132.0f;
        static constexpr float note_count_per_oct = 12.0f;
        static constexpr float max_oct_count = note_count / note_count_per_oct;
        static constexpr float note_height = 18.0f;
        static constexpr float note_height_padded = note_height + 1.0f;
        cursor_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("PianoRollKeys",
                               ImVec2(min_track_control_size, note_count * note_height_padded));
        ImGui::SameLine(0.0f, 2.0f);

        // Draw piano keys
        float keys_height = note_count_per_oct * note_height_padded;
        float oct_pos_y = main_cursor_pos.y - std::fmod(vscroll, keys_height);
        ImVec2 oct_pos = ImVec2(cursor_pos.x, oct_pos_y);
        uint32_t oct_count = (uint32_t)math::round(view_height / keys_height) + 1;
        int key_oct_offset =
            (uint32_t)(max_oct_count - std::floor(vscroll / keys_height)) - oct_count - 1;
        for (int i = oct_count; i >= 0; i--) {
            draw_piano_keys(draw_list, oct_pos, ImVec2(min_track_control_size, note_height),
                            i + key_oct_offset);
        }

        double view_scale = calc_view_scale();
        double inv_view_scale = 1.0 / view_scale;
        cursor_pos = ImGui::GetCursorScreenPos();
        ImVec2 region_size = ImGui::GetContentRegionAvail();
        timeline_width = region_size.x;

        float offset_y = vscroll + cursor_pos.y;
        ImVec2 view_min(cursor_pos.x, offset_y);
        ImVec2 view_max(cursor_pos.x + timeline_width, offset_y + region_size.y);
        ImGui::PushClipRect(view_min, view_max, true);

        ImGui::InvisibleButton("PianoRollContent",
                               ImVec2(region_size.x, note_count * note_height_padded));

        double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
        double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
        double clip_scale = ppq * inv_view_scale;

        float four_bars = (float)(16.0 * ppq / view_scale);
        uint32_t guidestrip_count = (uint32_t)(timeline_width / four_bars) + 2;
        float guidestrip_pos_x = cursor_pos.x - std::fmod((float)scroll_pos_x, four_bars * 2.0f);
        ImU32 guidestrip_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.13f);
        for (uint32_t i = 0; i <= guidestrip_count; i++) {
            float start_pos_x = guidestrip_pos_x;
            guidestrip_pos_x += four_bars;
            if (i % 2) {
                draw_list->AddRectFilled(ImVec2(start_pos_x, offset_y),
                                         ImVec2(guidestrip_pos_x, offset_y + region_size.y),
                                         guidestrip_color);
            }
        }

        ImU32 grid_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.55f);
        ImU32 beat_grid_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.15f);
        ImU32 bar_grid_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.3f);

        double beat = ppq / view_scale;
        double bar = 4.0 * beat;
        double division = std::exp2(std::round(std::log2(view_scale / 5.0)));
        float grid_inc_x = (float)(beat * division);
        float inv_grid_inc_x = 1.0f / grid_inc_x;
        uint32_t lines_per_bar = std::max((uint32_t)((float)bar / grid_inc_x), 1u);
        uint32_t lines_per_beat = std::max((uint32_t)((float)beat / grid_inc_x), 1u);
        float gridline_pos_x = cursor_pos.x - std::fmod((float)scroll_pos_x, grid_inc_x);
        int gridline_count = (uint32_t)(timeline_width * inv_grid_inc_x);
        int grid_index_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
        for (int i = 0; i <= gridline_count; i++) {
            gridline_pos_x += grid_inc_x;
            float gridline_pos_x_pixel = std::round(gridline_pos_x);
            uint32_t grid_id = i + grid_index_offset + 1;
            ImU32 line_color = grid_color;
            if (grid_id % lines_per_bar) {
                line_color = bar_grid_color;
            }
            if (grid_id % lines_per_beat) {
                line_color = beat_grid_color;
            }
            draw_list->AddLine(ImVec2(gridline_pos_x_pixel, offset_y),
                               ImVec2(gridline_pos_x_pixel, offset_y + region_size.y), line_color,
                               1.0f);
        }

        // Draw horizontal gridline
        float key_pos_y = main_cursor_pos.y - std::fmod(vscroll, note_height_padded);
        int num_keys = (int)math::round(view_height / note_height_padded);
        int key_index_offset = (int)(vscroll / note_height_padded);
        ImVec2 key_pos = ImVec2(cursor_pos.x, key_pos_y - 1.0f);
        for (int i = 0; i <= num_keys; i++) {
            uint32_t index = i + key_index_offset;
            uint32_t note_semitone = index % 12;
            draw_list->AddLine(key_pos, key_pos + ImVec2(timeline_width, 0.0f), grid_color);
            
            if (note_semitone / 7) {
                note_semitone++;
            }

            if (note_semitone % 2 == 0) {
                draw_list->AddRectFilled(key_pos + ImVec2(0.0f, 1.0f),
                                         key_pos + ImVec2(timeline_width, note_height_padded),
                                         guidestrip_color);
            }

            key_pos.y += note_height_padded;
        }

        static const ImU32 channel_color = ImColor(121, 166, 91);
        auto font = ImGui::GetFont();
        for (auto& note : midi_note.channels[0]) {
            char note_name[5] {};
            const char* scale = note_scale[note.note_number % 12];
            fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, note.note_number / 12);

            float pos_y = (float)(131 - note.note_number) * note_height_padded;
            float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * clip_scale);
            float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * clip_scale);
            ImVec2 min_bb(min_pos_x, cursor_pos.y + pos_y);
            ImVec2 max_bb(max_pos_x, cursor_pos.y + pos_y + note_height);
            ImVec4 label(min_bb.x, min_bb.y, max_bb.x - 6.0f, max_bb.y);
            draw_list->AddRectFilled(min_bb, max_bb, channel_color);
            draw_list->AddRect(min_bb, max_bb, 0x7F000000);
            draw_list->AddText(font, font->FontSize,
                               ImVec2(std::max(cursor_pos.x, min_pos_x) + 4.0f, min_bb.y + 2.0f),
                               0xFFFFFFFF, note_name, nullptr, 0.0f, &label);
        }

        ImGui::PopClipRect();

        ImGui::EndChild();
        ImGui::EndChild();
    }

    ImGui::End();
}

void GuiPianoRoll::draw_piano_keys(ImDrawList* draw_list, ImVec2& pos, const ImVec2& note_size,
                                   uint32_t oct) {
    ImU32 dark_note = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32 white_note = ImGui::GetColorU32(ImGuiCol_Text);
    ImU32 separator = ImGui::GetColorU32(ImGuiCol_Separator);
    uint32_t note_id = 11;
    for (int i = 0; i < 13; i++) {
        if (i == 7) {
            continue;
        }

        ImU32 bg_col;
        ImU32 text_col;

        if (i % 2) {
            bg_col = dark_note;
            text_col = white_note;
        } else {
            bg_col = white_note;
            text_col = dark_note;
        }

        char note_name[5] {};
        const char* scale = note_scale[note_id];
        fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, oct);

        draw_list->AddRectFilled(pos, pos + note_size, bg_col);
        draw_list->AddText(pos + ImVec2(4.0f, 2.0f), text_col, note_name);
        pos.y += note_size.y + 1.0f;
        note_id--;
    }
}

GuiPianoRoll g_piano_roll;
} // namespace wb