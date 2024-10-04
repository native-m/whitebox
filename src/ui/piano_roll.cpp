#include "piano_roll.h"
#include "controls.h"
#include "core/color.h"
#include "engine/engine.h"
#include "file_dialog.h"
#include <imgui.h>
#include <optional>

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

    ppq = g_engine.ppq;

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
    if (ImGui::BeginChild("PianoRoll", ImVec2(), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        render_horizontal_scrollbar();
        double new_time_pos = 0.0;
        if (render_time_ruler(&new_time_pos)) {
            // TODO
        }
        ImGui::PopStyleVar();

        ImVec2 content_origin = ImGui::GetCursorScreenPos();
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        float view_height = child_content_size.y;

        child_content_size = ImGui::GetContentRegionAvail();
        main_cursor_pos = cursor_pos;
        draw_list->AddLine(
            ImVec2(content_origin.x, content_origin.y - 1.0f),
            ImVec2(content_origin.x + ImGui::GetContentRegionAvail().x, content_origin.y - 1.0f),
            ImGui::GetColorU32(ImGuiCol_Separator));

        content_height = child_content_size.y * (1.0f - space_divider);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
        ImGui::BeginChild("PianoRollContent", ImVec2(0.0f, content_height));
        draw_list = ImGui::GetWindowDrawList();
        vscroll = ImGui::GetScrollY();

        ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
        if (scrolling && scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
            ImGui::SetScrollY(vscroll - scroll_delta_y);
            redraw = true;
        }

        if ((last_vscroll - vscroll) != 0.0f)
            redraw = true;

        float separator_x = cursor_pos.x + min_track_control_size + 0.5f;
        draw_list->AddLine(ImVec2(separator_x, cursor_pos.y),
                           ImVec2(separator_x, cursor_pos.y + content_height),
                           ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

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

        render_editor();
        ImGui::EndChild();

        if (controls::resizable_horizontal_separator("##PIANO_ROLL_SEPARATOR", &content_height,
                                                     0.25f * child_content_size.y, 0.0f,
                                                     child_content_size.y)) {
            space_divider = 1.0 - (content_height / child_content_size.y);
        }
        ImGui::PopStyleVar();

        ImGui::BeginChild("##PIANO_ROLL_EVENT");
        render_event_editor();
        ImGui::EndChild();

        ImGui::EndChild();
    }

    ImGui::End();
}

void GuiPianoRoll::render_editor() {
    double view_scale = calc_view_scale();
    double inv_view_scale = 1.0 / view_scale;
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    timeline_width = region_size.x;

    float offset_y = vscroll + cursor_pos.y;
    ImVec2 view_min(cursor_pos.x, offset_y);
    ImVec2 view_max(cursor_pos.x + timeline_width, offset_y + region_size.y);
    ImGui::PushClipRect(view_min, view_max, true);

    ImGui::InvisibleButton("PianoRollContent",
                           ImVec2(region_size.x, note_count * note_height_padded));

    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_wheel = ImGui::GetIO().MouseWheel;
    float mouse_wheel_h = ImGui::GetIO().MouseWheelH;
    bool holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
    bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool is_piano_roll_hovered = ImGui::IsItemHovered();

    if (is_piano_roll_hovered && mouse_wheel_h != 0.0f) {
        scroll_horizontal(mouse_wheel_h, song_length, -view_scale * 64.0);
    }

    // Assign scroll
    if (middle_mouse_clicked && middle_mouse_down && is_piano_roll_hovered)
        scrolling = true;

    // Do scroll
    if (scrolling) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
        scroll_horizontal(drag_delta.x, song_length, -view_scale);
        scroll_delta_y = drag_delta.y;
        if (scroll_delta_y != 0.0f)
            redraw = true;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    // Release scroll
    if (!middle_mouse_down) {
        scrolling = false;
        scroll_delta_y = 0.0f;
    }

    double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
    double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
    double clip_scale = ppq * inv_view_scale;

    // Draw guidestripes
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
    ImU32 bar_grid_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.3f);

    // Draw vertical gridlines
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

    // Draw horizontal gridlines
    float key_pos_y = main_cursor_pos.y - std::fmod(vscroll, note_height_padded);
    int num_keys = (int)math::round(content_height / note_height_padded);
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

    static const ImU32 channel_color = color_brighten(ImColor(121, 166, 91), 0.6f);
    static const ImU32 text_color = color_darken(ImColor(121, 166, 91), 1.25f);
    auto font = ImGui::GetFont();
    float end_x = cursor_pos.x + timeline_width;
    float end_y = main_cursor_pos.y + content_height;
    std::optional<uint32_t> note_id;
    for (auto& note : midi_note.channels[0]) {
        float pos_y = (float)(131 - note.note_number) * note_height_padded;
        float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * clip_scale);
        float max_pos_x = (float)math::round(scroll_offset_x + note.max_time * clip_scale);
        if (max_pos_x < cursor_pos.x)
            continue;
        if (min_pos_x > end_x)
            break;

        float min_pos_y = cursor_pos.y + pos_y;
        float max_pos_y = min_pos_y + note_height;
        ImRect note_rect(min_pos_x, min_pos_y, max_pos_x, max_pos_y);
        if (note_rect.Contains(mouse_pos) && is_piano_roll_hovered)
            note_id = note.id;

        ImVec2 a(min_pos_x + 0.5f, min_pos_y + 0.5f);
        ImVec2 b(max_pos_x - 0.5f, max_pos_y - 0.5f);
        if (a.y > end_y || b.y < main_cursor_pos.y)
            continue;

        draw_list->PathLineTo(a);
        draw_list->PathLineTo(ImVec2(b.x, a.y));
        draw_list->PathLineTo(b);
        draw_list->PathLineTo(ImVec2(a.x, b.y));
        draw_list->PathFillConvex(channel_color);
        // draw_list->AddRectFilled(min_bb, max_bb, channel_color);
        // draw_list->AddRect(min_bb, max_bb, 0x7F000000);

        ImVec4 label_rect(a.x, a.y, b.x - 4.0f, b.y);
        char note_name[5] {};
        const char* scale = note_scale[note.note_number % 12];
        fmt::format_to_n(note_name, sizeof(note_name), "{}{}", scale, note.note_number / 12);
        draw_list->AddText(font, font->FontSize,
                           ImVec2(std::max(cursor_pos.x, min_pos_x) + 3.0f, a.y + 2.0f), text_color,
                           note_name, nullptr, 0.0f, &label_rect);
    }

    if (note_id) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        Log::debug("{}", note_id.value());
    }

    if (is_piano_roll_hovered && holding_ctrl && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel);
        force_redraw = true;
    }

    last_vscroll = vscroll;

    ImGui::PopClipRect();
}

void GuiPianoRoll::render_event_editor() {
    auto draw_list = ImGui::GetWindowDrawList();
    auto cursor_pos = ImGui::GetCursorScreenPos() + ImVec2(min_track_control_size, 0.0f);
    auto editor_event_region = ImGui::GetContentRegionAvail();
    double view_scale = calc_view_scale();
    double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);
    double scroll_offset_x = (double)cursor_pos.x - scroll_pos_x;
    double clip_scale = ppq / view_scale;
    float end_x = cursor_pos.x + timeline_width;
    float end_y = cursor_pos.y + editor_event_region.y;
    static const ImU32 channel_color = color_brighten(ImColor(121, 166, 91), 0.6f);

    for (auto& note : midi_note.channels[0]) {
        float min_pos_x = (float)math::round(scroll_offset_x + note.min_time * clip_scale);
        if (min_pos_x < cursor_pos.x)
            continue;
        if (min_pos_x > end_x)
            break;
        float min_pos_y = cursor_pos.y + (1.0f - note.velocity) * editor_event_region.y;
        ImVec2 min_pos = ImVec2(min_pos_x, min_pos_y);
        draw_list->AddLine(min_pos, ImVec2(min_pos_x, end_y), channel_color);
        draw_list->AddRectFilled(min_pos - ImVec2(2.0f, 2.0f), min_pos + ImVec2(3.0f, 3.0f),
                                 channel_color);
    }

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
        if (i == 12) {
            bg_col = 0xFFAFAFAF;
            text_col = dark_note;
        } else if (i % 2) {
            bg_col = dark_note;
            text_col = white_note;
        } else {
            bg_col = 0xFFEFEFEF;
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