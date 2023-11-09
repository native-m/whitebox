#include "gui_timeline.h"
#include "gui_content_browser.h"
#include "global_state.h"
#include "widget.h"
#include "controls.h"
#include "popup_state_manager.h"
#include "core/color.h"
#include "engine/sample_table.h"
#include <imgui_stdlib.h>

namespace wb
{
    GUITimeline g_gui_timeline;

    GUITimeline::GUITimeline()
    {
    }

    void GUITimeline::render_track_header(Track& track)
    {
        auto& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, style.ItemSpacing.y));
        ImGui::BeginMenuBar();

        auto cursor_pos = ImGui::GetCursorScreenPos();
        auto size = ImGui::GetWindowContentRegionMax();

        ImGui::PushClipRect(cursor_pos, ImVec2(cursor_pos.x + size.x - 5.f, cursor_pos.y + size.y), true);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4());
        widget::collapse_button("##track_collapse", &track.shown);
        ImGui::Text((const char*)track.name.c_str());
        ImGui::PopStyleColor();
        ImGui::PopClipRect();

        ImVec4 bg_color = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        bg_color.w = 1.0f;

        //ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(2.0f, 2.0f));
        ImGui::SameLine(size.x - ImGui::GetStyle().ItemInnerSpacing.x - ImGui::GetFontSize() - 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Checkbox("##track_active", &track.active);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::EndMenuBar();
        ImGui::PopStyleVar();
    }

    void GUITimeline::initialize()
    {
    }

    void GUITimeline::redraw_clip_content()
    {
        force_redraw_clip_content = true;
    }

    void GUITimeline::render_track_context_menu(Track& track, int track_id)
    {
        auto states = ImGui::GetStateStorage();
        bool rename_track = false;
        bool delete_track = false;
        bool show_track_context_menu = false;

        if (ImGui::BeginPopup("track_context_menu")) {
            ImGui::MenuItem(track.name.c_str(), nullptr, false, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) {
                ImGui::CloseCurrentPopup();
                rename_track = true;
            }
            ImGui::MenuItem("Duplicate");
            if (ImGui::MenuItem("Delete")) {
                g_engine.delete_track((uint32_t)track_id);
            }
            ImGui::MenuItem("Change color...");
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Height")) {
                ImGui::CloseCurrentPopup();
                should_redraw_clip_content = true;
                track.height = 56.0f;
            }
            ImGui::EndPopup();
        }

        if (rename_track)
            ImGui::OpenPopup("rename_track_popup");

        auto is_renaming = states->GetBoolRef(ImGui::GetID("is_renaming"));
        auto rename_track_str_id = ImGui::GetID("rename_track_str");

        *is_renaming = false;
        if (ImGui::BeginPopup("rename_track_popup")) {
            *is_renaming = true;
            auto rename_str = (std::string**)states->GetVoidPtrRef(rename_track_str_id);
            if (!*rename_str)
                *rename_str = new std::string(track.name);

            ImGui::Text("Rename Track");

            bool change = ImGui::InputTextWithHint("##new_name", "New name", *rename_str, ImGuiInputTextFlags_EnterReturnsTrue);
            change |= ImGui::Button("Ok");

            if (change) {
                ImGui::CloseCurrentPopup();
                track.name = **rename_str;
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if (!*is_renaming) {
            auto rename_str = (std::string**)states->GetVoidPtrRef(rename_track_str_id);
            if (*rename_str) {
                // TODO: delete the string when quitting.
                delete* rename_str;
                *rename_str = nullptr;
            }
        }
    }

    void GUITimeline::render_track_controls(Track& track)
    {
        ImGui::DragFloat("Vol.", &track.volume);
    }

    void GUITimeline::render_clip_context_menu()
    {
        if (!(g_selected_clip && g_selected_track))
            return;

        bool change_color = false;

        if (ImGui::BeginPopup("clip_context_menu")) {
            if (ImGui::MenuItem("Change Color...")) {
                ImGui::CloseCurrentPopup();
                change_color = true;
            }
            ImGui::MenuItem("Rename...");
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                g_engine.delete_clip(g_selected_track, g_selected_clip);
                g_selected_clip = nullptr;
                g_selected_track = nullptr;
            }
            ImGui::EndPopup();
        }

        if (change_color)
            ImGui::OpenPopup("change_clip_color");

        if (ImGui::BeginPopup("change_clip_color")) {
            PopupStateContext state;
            int* color_value = state.GetIntRef(ImGui::GetID("clip_color"), std::bit_cast<int>((ImU32)g_selected_clip->color));
            assert(color_value != nullptr);

            ImGui::Text("Change color");
            ImGui::Separator();

            ImColor color(std::bit_cast<ImU32>(*color_value));
            if (ImGui::ColorPicker4("Color##clip_color_picker", (float*)&color, ImGuiColorEditFlags_NoAlpha))
                *color_value = color;

            ImGui::Separator();

            if (ImGui::Button("Ok")) {
                g_selected_clip->color = color;
                g_selected_clip = nullptr;
                g_selected_track = nullptr;
                ImGui::CloseCurrentPopup();
                redraw_clip_content();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void GUITimeline::render_horizontal_scrollbar()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        auto draw_list = ImGui::GetWindowDrawList();
        auto font_size = ImGui::GetFontSize();
        auto btn_size_y = font_size + style.FramePadding.y * 2.0f;
        auto arrow_btn_size = ImGui::CalcItemSize(ImVec2(), font_size + style.FramePadding.x * 2.0f, btn_size_y);
        ImGui::SetCursorPosX(std::max(separator_x, 100.0f) + 2.0f);
        ImGui::PushButtonRepeat(true);

        if (ImGui::Button("<", arrow_btn_size))
            do_horizontal_scroll_drag(-0.05f, 1.0f);

        auto scroll_btn_length = ImGui::GetContentRegionAvail().x - arrow_btn_size.x;
        ImGui::SameLine();
        auto scroll_btn_min_bb = ImGui::GetCursorScreenPos();
        ImGui::SameLine(scroll_btn_length);
        auto scroll_btn_max_bb = ImGui::GetCursorScreenPos();

        if (ImGui::Button(">", arrow_btn_size))
            do_horizontal_scroll_drag(0.05f, 1.0f);

        ImGui::PopButtonRepeat();

        // Add gap between arrow buttons and scroll grab
        scroll_btn_min_bb.x += 1.0f;
        scroll_btn_max_bb.x -= 1.0f;

        auto scroll_btn_max_length = scroll_btn_max_bb.x - scroll_btn_min_bb.x;
        ImGui::SetCursorScreenPos(scroll_btn_min_bb);
        ImGui::InvisibleButton("##timeline_hscroll", ImVec2(scroll_btn_max_length, btn_size_y));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        bool scrolling = resizing_lhs_scroll_grab || resizing_rhs_scroll_grab || grabbing_scroll;
        
        if (scrolling)
            should_redraw_clip_content = true;

        if (!active && scrolling) {
            resizing_lhs_scroll_grab = false;
            resizing_rhs_scroll_grab = false;
            grabbing_scroll = false;
            ImGui::ResetMouseDragDelta();
        }

        if (hovered)
            do_horizontal_scroll_drag(ImGui::GetIO().MouseWheel, 1.0f, -0.05f * (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? 0.1f : 0.5f));

        // Remap scroll units in pixels
        float min_scroll_pos_x_pixels = (float)min_scroll_pos_x * scroll_btn_max_length;
        float max_scroll_pos_x_pixels = (1.0f - (float)max_scroll_pos_x) * scroll_btn_max_length;

        // Calculate bounds
        ImVec2 lhs_min(scroll_btn_min_bb.x + min_scroll_pos_x_pixels, scroll_btn_min_bb.y);
        ImVec2 lhs_max(scroll_btn_min_bb.x + min_scroll_pos_x_pixels + 2.0f, scroll_btn_min_bb.y + btn_size_y);
        ImVec2 rhs_min(scroll_btn_max_bb.x - max_scroll_pos_x_pixels - 2.0f, scroll_btn_min_bb.y);
        ImVec2 rhs_max(scroll_btn_max_bb.x - max_scroll_pos_x_pixels, scroll_btn_min_bb.y + btn_size_y);

        // Check whether the mouse hovering the left-hand side bound
        if (!grabbing_scroll && ImGui::IsMouseHoveringRect(lhs_min, lhs_max)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (active && !resizing_lhs_scroll_grab)
                resizing_lhs_scroll_grab = true;
        }
        // Check whether the mouse hovering the right-hand side bound
        else if (!grabbing_scroll && ImGui::IsMouseHoveringRect(rhs_min, rhs_max)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (active && !resizing_rhs_scroll_grab)
                resizing_rhs_scroll_grab = true;
        }
        // Check whether the mouse is grabbing the scroll
        else if (ImGui::IsMouseHoveringRect(lhs_min, rhs_max) && active && !grabbing_scroll) {
            last_min_scroll_pos_x = min_scroll_pos_x;
            grabbing_scroll = true;
        }
        // Check whether the mouse clicking on the scroll area
        else if (ImGui::IsItemActivated()) {
            double scroll_grab_length = max_scroll_pos_x - min_scroll_pos_x;
            double half_scroll_grab_length = scroll_grab_length * 0.5;
            auto mouse_pos_x = (ImGui::GetMousePos().x - scroll_btn_min_bb.x) / (double)scroll_btn_max_length;
            double new_min_scroll_pos_x = std::clamp(mouse_pos_x - half_scroll_grab_length, 0.0, 1.0 - scroll_grab_length);
            max_scroll_pos_x = new_min_scroll_pos_x + scroll_grab_length;
            min_scroll_pos_x = new_min_scroll_pos_x;
            should_redraw_clip_content = true;
        }

        if (resizing_lhs_scroll_grab) {
            auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            min_scroll_pos_x = std::clamp(min_scroll_pos_x + drag_delta.x / scroll_btn_max_length, 0.0, max_scroll_pos_x);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left); 
        }
        else if (resizing_rhs_scroll_grab) {
            auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            max_scroll_pos_x = std::clamp(max_scroll_pos_x + drag_delta.x / scroll_btn_max_length, min_scroll_pos_x, 1.0);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        else if (grabbing_scroll) {
            ImVec2 drag_delta = ImGui::GetMouseDragDelta();
            double scroll_grab_length = max_scroll_pos_x - min_scroll_pos_x;
            double new_min_scroll_pos_x = std::clamp(last_min_scroll_pos_x + drag_delta.x / scroll_btn_max_length, 0.0, 1.0 - scroll_grab_length);
            max_scroll_pos_x = new_min_scroll_pos_x + scroll_grab_length;
            min_scroll_pos_x = new_min_scroll_pos_x;
        }

        draw_list->AddRectFilled(lhs_min, rhs_max, ImGui::GetColorU32(ImGuiCol_Button), style.GrabRounding);
        if (hovered || active)
            draw_list->AddRect(lhs_min, rhs_max, active ? ImGui::GetColorU32(ImGuiCol_FrameBgActive) : ImGui::GetColorU32(ImGuiCol_FrameBgHovered), style.GrabRounding);
    }

    void GUITimeline::render_time_ruler()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        auto col = ImGui::GetColorU32(ImGuiCol_Separator, 1.0f);
        auto draw_list = ImGui::GetWindowDrawList();
        auto mouse_pos = ImGui::GetMousePos();
        ImGui::SetCursorPosX(std::max(separator_x, 100.0f) + 2.0f);

        auto cursor_pos = ImGui::GetCursorScreenPos();
        auto size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
        float view_scale = (float)((max_scroll_pos_x - min_scroll_pos_x) * music_length) / timeline_width;
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        ImGui::InvisibleButton("##time_ruler_control", size);

        if (ImGui::IsItemActivated() || (ImGui::IsItemActive() && std::abs(drag_delta.x) > 0.001f)) {
            double mapped_x_pos = (double)(mouse_pos.x - cursor_pos.x) / music_length * (double)view_scale + min_scroll_pos_x;
            double mouse_time_pos = mapped_x_pos * music_length / 96.0;
            double mouse_time_pos_grid = std::max(std::round(mouse_time_pos * grid_scale) / grid_scale, 0.0);
            g_engine.set_play_position(mouse_time_pos_grid);
            ImGui::ResetMouseDragDelta();
        }

        // Handle zoom scrolling on ruler
        float mouse_wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::IsItemHovered() && mouse_wheel != 0.0f) {
            do_zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel);
            view_scale = (float)((max_scroll_pos_x - min_scroll_pos_x) * music_length) / timeline_width;
        }

        auto font = ImGui::GetFont();
        float grid_inc_x = 96.0f * 4.0f / view_scale;
        float inv_grid_inc_x = 1.0f / grid_inc_x;
        float scroll_pos_x = (float)(min_scroll_pos_x * music_length) / view_scale;
        float gridline_pos_x = cursor_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
        float scroll_offset = cursor_pos.x - scroll_pos_x;
        int line_count = (uint32_t)(size.x * inv_grid_inc_x) + 1;
        int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);

        draw_list->PushClipRect(cursor_pos, ImVec2(cursor_pos.x + size.x, cursor_pos.y + size.y));

        bool is_playing = g_engine.is_playing();
        if (is_playing) {
            float play_position = std::round(scroll_offset + map_playhead_to_screen_position(view_scale, g_engine.play_time)) - size.y * 0.5f;
            draw_list->AddTriangleFilled(ImVec2(play_position, cursor_pos.y + 2.5f),
                                         ImVec2(play_position + size.y, cursor_pos.y + 2.5f),
                                         ImVec2(play_position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f), col);
        }

        for (int i = 0; i <= line_count; i++) {
            char digits[24]{};
            float rounded_gridline_pos_x = std::round(gridline_pos_x);
            fmt::format_to_n(digits, sizeof(digits), "{}", i + count_offset);
            draw_list->AddText(ImVec2(rounded_gridline_pos_x + 4.0f, cursor_pos.y + style.FramePadding.y * 2.0f - 2.0f), ImGui::GetColorU32(ImGuiCol_Text), digits);
            draw_list->AddLine(ImVec2(rounded_gridline_pos_x, cursor_pos.y + size.y - 8.0f),
                               ImVec2(rounded_gridline_pos_x, cursor_pos.y + size.y - 3.0f),
                               col);
            gridline_pos_x += grid_inc_x;
        }

        float playhead_screen_position = std::round(scroll_offset + map_playhead_to_screen_position(view_scale, playhead_position)) - size.y * 0.5f;
        draw_list->AddTriangleFilled(ImVec2(playhead_screen_position, cursor_pos.y + 2.5f),
                                     ImVec2(playhead_screen_position + size.y, cursor_pos.y + 2.5f),
                                     ImVec2(playhead_screen_position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f),
                                     playhead_color);

        draw_list->PopClipRect();
    }

    /*
        TODO:
        - Implement Clip Nodes (done)
        - Implement UI controls/input (done)
        - Implement sample visualization (done, probably decimate the samples with LTTB algorithm)
    */
    void GUITimeline::render()
    {
        constexpr ImGuiWindowFlags track_control_window_flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_AlwaysUseWindowPadding;

        constexpr ImGuiWindowFlags timeline_content_area_flags =
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_AlwaysVerticalScrollbar;

        constexpr ImDrawListFlags draw_list_aa_flags =
            ImDrawListFlags_AntiAliasedFill |
            ImDrawListFlags_AntiAliasedLinesUseTex |
            ImDrawListFlags_AntiAliasedLines;

        if (!g_show_timeline_window)
            return;

        //ImGuiWindowClass WindowClass;
        //WindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
        //ImGui::SetNextWindowClass(&WindowClass);
        ImGui::SetNextWindowSize(ImVec2(640.f, 480.f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
        if (!controls::begin_dockable_window("Timeline", &g_show_timeline_window)) {
            ImGui::PopStyleVar();
            ImGui::End();
            return;
        }
        ImGui::PopStyleVar();

        // Force timeline to redraw clip contents
        should_redraw_clip_content = force_redraw_clip_content;
        if (force_redraw_clip_content)
            force_redraw_clip_content = false;

        playhead_position = g_engine.get_playhead_position();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        render_horizontal_scrollbar();
        render_time_ruler();
        ImGui::PopStyleVar();

        if (zooming) {
            // Lock y-axis scroll while zooming.
            ImGui::SetNextWindowScroll(ImVec2(0.0f, last_scroll_pos_y));
            zooming = false;
            should_redraw_clip_content = true;
        }
        
        auto draw_pos = ImGui::GetCursorScreenPos();
        auto draw_list = ImGui::GetWindowDrawList();
        auto min_window_content = ImGui::GetWindowContentRegionMin();
        auto window_size = ImGui::GetWindowContentRegionMax();

        window_size.x -= min_window_content.x;
        draw_list->AddLine(ImVec2(draw_pos.x, draw_pos.y - 1.0f), ImVec2(draw_pos.x + window_size.x, draw_pos.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));

        ImGui::BeginChild("##timeline_content", ImVec2(0.0f, 0.0f), false, timeline_content_area_flags);
        auto timeline_content_window = ImGui::GetCurrentWindow();
        if (ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(timeline_content_window, ImGuiAxis_Y))
            should_redraw_clip_content = true;

        if (scrolling && std::abs(scroll_delta_y) > 0.0f) {
            ImGui::SetScrollY(ImGui::GetScrollY() - scroll_delta_y);
            should_redraw_clip_content = true;
        }

        draw_pos = ImGui::GetCursorScreenPos();
        draw_list = ImGui::GetWindowDrawList();

        float mouse_wheel = ImGui::GetIO().MouseWheel;
        auto mouse_pos = ImGui::GetMousePos();
        auto cursor_orig = ImGui::GetCursorPos();
        auto scroll_y = ImGui::GetScrollY();
        auto scroll_offset_y = draw_pos.y + scroll_y;
        auto inv_scroll_offset_y = draw_pos.y - scroll_y;
        const auto& style = ImGui::GetStyle();
        float font_size = ImGui::GetFontSize();
        bool mouse_move = false;

        min_window_content = ImGui::GetWindowContentRegionMin();
        window_size = ImGui::GetWindowContentRegionMax();
        window_size.x -= min_window_content.x;
        window_size.y -= min_window_content.y;

        if (mouse_pos.x != last_mouse_pos.x || mouse_pos.y != last_mouse_pos.y) {
            last_mouse_pos = mouse_pos;
            mouse_move = true;
        }

        if ((last_scroll_pos_y - scroll_y) != 0.0f)
            should_redraw_clip_content = true;

        // A separator between track controls and its timeline lane
        ImGui::SetCursorScreenPos(ImVec2(draw_pos.x + separator_x - 2.0f, scroll_offset_y));
        ImGui::InvisibleButton("timeline_separator", ImVec2(4, window_size.y));

        bool is_separator_active = ImGui::IsItemActive();
        bool is_separator_hovered = ImGui::IsItemHovered();

        if (is_separator_hovered || is_separator_active) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                separator_x = 150.0f;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        if (is_separator_active) {
            auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            separator_x += drag_delta.x;
            should_redraw_clip_content = true;
        }
        else {
            separator_x = std::max(separator_x, 100.0f);
        }

        float clamped_separator = std::max(separator_x, 100.0f);

        ImGui::SetCursorPos(cursor_orig);
        ImGui::PushClipRect(ImVec2(draw_pos.x, draw_pos.y), ImVec2(draw_pos.x + clamped_separator, draw_pos.y + window_size.y + ImGui::GetScrollY()), true);

        // Render track controls
        int id = 0;
        static float drag_test = 0.0f;
        static float frame_bg_alpha = 0.1f;
        static float track_color_width = 8.0f;
        //auto& frame_bg_color = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        for (auto& track : g_engine.tracks) {
            auto tmp_item_spacing = style.ItemSpacing;
            auto frame_bg_accent = color_adjust_alpha(track->color, frame_bg_alpha);
            ImVec2 track_color_min = ImGui::GetCursorScreenPos();
            ImVec2 track_color_max = ImVec2(track_color_min.x + track_color_width, track_color_min.y + track->height);

            ImGui::Indent(track_color_width);
            ImGui::PushID(id);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(track_color_width, 2.0f));
            ImGui::BeginChild("##track_control", ImVec2(clamped_separator - track_color_width, track->height), false, track_control_window_flags);
            {
                ImGui::PopStyleVar();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
                widget::collapse_button("##track_collapse", &track->shown);
                ImGui::PopStyleVar();
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::Text((const char*)track->name.c_str());

                render_track_controls(*track);

                if (ImGui::IsWindowHovered() && !(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup("track_context_menu");
                }

                render_track_context_menu(*track, id);
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::Unindent(track_color_width);
            
            if (widget::hseparator_resizer(id, &track->height, 56.0f, 30.f, 500.f))
                should_redraw_clip_content = true;

            ImGui::PopStyleVar();

            draw_list->AddRectFilled(track_color_min, track_color_max, ImGui::GetColorU32((ImVec4)track->color));

            id++;
        }

        if (ImGui::Button("Add Audio Track")) {
            auto track = g_engine.add_track(TrackType::Audio, "New track");
            track->color = ImColor::HSV((float)current_clip_n / 15, 0.5f, 0.7f);
            current_clip_n = (current_clip_n + 1) % 15;
        }

        ImGui::PopClipRect();
        auto end_cursor = ImGui::GetCursorPos();

        // Calculate view scale (zoom)
        float timeline_orig_pos_x = draw_pos.x + clamped_separator + 2.0f;
        float timeline_orig_pos_x_rounded = std::round(timeline_orig_pos_x);
        ImGui::SetCursorScreenPos(ImVec2(timeline_orig_pos_x, draw_pos.y));

        auto timeline_area = ImGui::GetContentRegionAvail();
        ImGui::PushClipRect(ImVec2(timeline_orig_pos_x, scroll_offset_y),
                            ImVec2(timeline_orig_pos_x + timeline_width, timeline_area.y + scroll_offset_y),
                            true);

        // Re-create clip content framebuffer
        if (timeline_view_width != (int)timeline_area.x || timeline_view_height != (int)timeline_area.y) {
            timeline_view_width = (int)timeline_area.x;
            timeline_view_height = (int)timeline_area.y;
            clip_content_fb = Renderer::instance->create_framebuffer(timeline_view_width, timeline_view_height);
            should_redraw_clip_content = true;
        }

        double view_scale = ((max_scroll_pos_x - min_scroll_pos_x) * music_length) / (double)timeline_area.x;
        double sample_scale = 96.0 / (view_scale * get_output_sample_rate() * g_engine.beat_duration.load(std::memory_order_relaxed));
        double inv_sample_scale = 1.0 / sample_scale;
        double inv_view_scale = 1.0 / view_scale;
        timeline_width = timeline_area.x;
        ImGui::InvisibleButton("##timeline", ImVec2(timeline_width, std::max(timeline_area.y, end_cursor.y)));

        bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool timeline_hovered = ImGui::IsItemHovered();

        if (middle_mouse_clicked && middle_mouse_down && timeline_hovered)
            scrolling = true;

        if (scrolling) {
            ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
            do_horizontal_scroll_drag(drag_delta.x, music_length, -view_scale);
            scroll_delta_y = drag_delta.y;
            if (mouse_move)
                should_redraw_clip_content = true;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }

        if (!middle_mouse_down) {
            scrolling = false;
            scroll_delta_y = 0.0f;
        }

        // Handles file drag & drop
        ContentBrowserFilePayload item_drop{};
        bool dragging_file = false;
        if (ImGui::BeginDragDropTarget()) {
            constexpr ImGuiDragDropFlags drag_drop_flags = ImGuiDragDropFlags_AcceptPeekOnly | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
            if (ImGui::AcceptDragDropPayload("WB_FILEDROP", drag_drop_flags)) {
                auto drop_payload = ImGui::AcceptDragDropPayload("WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (drop_payload)
                    std::memcpy(&item_drop, drop_payload->Data, drop_payload->DataSize);
                dragging_file = true;
                should_redraw_clip_content = true;
            }
            ImGui::EndDragDropTarget();
        }

        // Do automatic horizontal scroll when moving/resizing clips or dragging items to the edge of timeline
        float timeline_end_x = timeline_orig_pos_x + timeline_width;
        if (clip_action != GUITimelineClipAction::None || dragging_file) {
            float min_offset = !dragging_file ? timeline_orig_pos_x : timeline_orig_pos_x + 20.0f;
            float max_offset = !dragging_file ? timeline_end_x : timeline_end_x - 20.0f;
            if (mouse_pos.x < min_offset) {
                float distance = min_offset - mouse_pos.x;
                do_horizontal_scroll_drag(distance * 0.25f * (float)inv_view_scale, music_length, -view_scale);
            }
            if (mouse_pos.x > max_offset) {
                float distance = max_offset - mouse_pos.x;
                do_horizontal_scroll_drag(distance * 0.25f * (float)inv_view_scale, music_length, -view_scale);
            }
        }

        // ------------- Render track grid lines -------------
        auto grid_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.5f);
        float grid_inc_x = (float)(96.0 / view_scale / (double)grid_scale);
        float inv_grid_inc_x = 1.0f / grid_inc_x;
        double scroll_pos_x = (min_scroll_pos_x * music_length) / view_scale;
        float gridline_pos_x = timeline_orig_pos_x - std::fmod((float)scroll_pos_x, grid_inc_x);
        int line_count = (uint32_t)(timeline_width * inv_grid_inc_x);
        int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
        for (int i = 0; i <= line_count; i++) {
            gridline_pos_x += grid_inc_x;
            draw_list->AddLine(ImVec2(std::round(gridline_pos_x), scroll_offset_y),
                               ImVec2(std::round(gridline_pos_x), scroll_offset_y + window_size.y),
                               grid_color, (i + count_offset + 1) % 4 ? 1.0f : 2.0f);
        }
        
        uint32_t track_separator_color = ImGui::GetColorU32(ImGuiCol_Separator); // Remove transparency
        ImColor text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        auto font = ImGui::GetFont();
        float track_pos_y = draw_pos.y;
        double timeline_scroll_offset_x = (double)timeline_orig_pos_x - scroll_pos_x;
        float timeline_scroll_offset_x_f32 = (float)timeline_scroll_offset_x;
        double mapped_x_pos = (double)(mouse_pos.x - timeline_orig_pos_x) / music_length * view_scale + min_scroll_pos_x;
        double mouse_time_pos = mapped_x_pos * music_length / 96.0;
        double mouse_pos_time_grid = std::round(mouse_time_pos * grid_scale) / grid_scale;
        float clip_scale = (float)(inv_view_scale * 96.0);
        ImDrawListFlags old_draw_list = draw_list->Flags;
        ImDrawListFlags disable_aa = draw_list->Flags & ~draw_list_aa_flags;
        
        if (clip_action != GUITimelineClipAction::None && mouse_move)
            should_redraw_clip_content = true;

        // Apply clip action
        switch (clip_action) {
            case GUITimelineClipAction::Move:
                if (!left_mouse_down) {
                    g_engine.move_clip(g_selected_track, g_selected_clip, mouse_pos_time_grid - initial_move_pos);
                    finish_clip_action();
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                break;
            case GUITimelineClipAction::ResizeLeft:
                if (!left_mouse_down) {
                    g_engine.resize_clip(g_selected_track, g_selected_clip, mouse_pos_time_grid - initial_move_pos, false);
                    finish_clip_action();
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case GUITimelineClipAction::ResizeRight:
                if (!left_mouse_down) {
                    g_engine.resize_clip(g_selected_track, g_selected_clip, mouse_pos_time_grid - initial_move_pos, true);
                    finish_clip_action();
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case GUITimelineClipAction::Duplicate:
                if (!left_mouse_down) {
                    double clip_length = g_selected_clip->max_time - g_selected_clip->min_time;
                    AudioClip* new_clip = g_engine.add_audio_clip(g_selected_track, mouse_pos_time_grid, mouse_pos_time_grid + clip_length);
                    new_clip->name = g_selected_clip->name;
                    new_clip->asset = static_cast<AudioClip*>(g_selected_clip)->asset;
                    new_clip->color = g_selected_clip->color;
                    should_redraw_clip_content = true;
                    finish_clip_action();
                }
                break;
            case GUITimelineClipAction::ContextMenu:
                ImGui::OpenPopup("clip_context_menu");
                clip_action = GUITimelineClipAction::None;
                break;
            default:
                break;
        }

        bool has_deleted_clips = g_engine.has_deleted_clips.test(std::memory_order_relaxed);
        double beat_duration = g_engine.beat_duration.load(std::memory_order_relaxed);
        clip_content_draw_list.resize(0);

        // ------------- Render tracks -------------
        for (auto& track : g_engine.tracks) {
            float height = track->height;
            // Skip out-of-screen tracks.
            if (track_pos_y > window_size.y + scroll_offset_y) {
                break;
            }

            if (track_pos_y < scroll_offset_y - height - 2.0f) {
                track_pos_y += height + 2.0f;
                continue;
            }
            
            bool hovering_track_rect = !scrolling && ImGui::IsMouseHoveringRect(ImVec2(timeline_orig_pos_x, track_pos_y),
                                                                                ImVec2(timeline_orig_pos_x + timeline_width, track_pos_y + height));
            bool hovering_current_track = timeline_hovered && hovering_track_rect;

            // Handle file drag & drop
            if (hovering_track_rect && dragging_file) {
                // Highlight drop target
                float highlight_pos = (float)mouse_pos_time_grid; // Snap to grid
                draw_list->AddRectFilled(ImVec2(timeline_scroll_offset_x_f32 + highlight_pos * clip_scale, track_pos_y),
                                         ImVec2(timeline_scroll_offset_x_f32 + (highlight_pos + 1.0f) * clip_scale, track_pos_y + height),
                                         ImGui::GetColorU32(ImGuiCol_Border));

                // We have file dropped
                if (item_drop.item) {
                    std::filesystem::path file_path = item_drop.item->get_file_path(*item_drop.root_dir);
                    auto sample_asset = g_engine.get_or_load_sample_asset(file_path);
                    if (sample_asset) {
                        AudioClip* clip = g_engine.add_audio_clip(track.get(), highlight_pos, highlight_pos + 2.0f);
                        clip->name = (*sample_asset)->name;
                        clip->asset = std::move(*sample_asset);
                    }
                    Log::info("Dropped at: {}", mapped_x_pos);
                }
            }

            // Render clips
            Clip* current_clip = (Clip*)track->head_node.next;
            while (current_clip != &track->tail_node) {
                if (has_deleted_clips && track->deleted_clips.contains(current_clip)) {
                    current_clip = (Clip*)current_clip->next;
                    Log::info("Deleted clips skipped");
                    continue;
                }

                double min_time = current_clip->min_time;
                double max_time = current_clip->max_time;

                if (current_clip == g_selected_clip) {
                    switch (clip_action) {
                        case GUITimelineClipAction::Move:
                        {
                            double new_min_time = std::max(min_time + mouse_pos_time_grid - initial_move_pos, 0.0);
                            max_time = new_min_time + (max_time - min_time);
                            min_time = new_min_time;
                            
                            // Readjust music length and scrolling range
                            if (max_time * 96.0f > music_length) {
                                double new_music_length = std::max(max_time * 96.0f, music_length);
                                min_scroll_pos_x = min_scroll_pos_x * music_length / new_music_length;
                                max_scroll_pos_x = max_scroll_pos_x * music_length / new_music_length;
                                music_length = new_music_length;
                            }
                            break;
                        }
                        case GUITimelineClipAction::ResizeLeft:
                            min_time = std::max(min_time + mouse_pos_time_grid - initial_move_pos, 0.0);
                            if (min_time >= max_time)
                                min_time = max_time - 1.0f;
                            break;
                        case GUITimelineClipAction::ResizeRight:
                            max_time = std::max(max_time + mouse_pos_time_grid - initial_move_pos, 0.0);
                            if (max_time <= min_time)
                                max_time = min_time + 1.0f;
                            break;
                        case GUITimelineClipAction::Duplicate:
                        {
                            float highlight_pos = (float)mouse_pos_time_grid; // Snap to grid
                            float length = (float)(g_selected_clip->max_time - g_selected_clip->min_time);
                            draw_list->AddRectFilled(ImVec2(timeline_scroll_offset_x_f32 + highlight_pos * clip_scale, track_pos_y),
                                                     ImVec2(timeline_scroll_offset_x_f32 + (highlight_pos + length) * clip_scale, track_pos_y + height),
                                                     ImGui::GetColorU32(ImGuiCol_Border));
                            break;
                        }
                        default:
                            break;
                    }
                }

                double min_pos_x = min_time * (double)clip_scale;
                double max_pos_x = max_time * (double)clip_scale;
                float min_pos_x_in_pixel = (float)std::round(timeline_scroll_offset_x + min_pos_x);
                float max_pos_x_in_pixel = (float)std::round(timeline_scroll_offset_x + max_pos_x);

                // Skip out-of-screen clips.
                if (min_pos_x_in_pixel > timeline_end_x)
                    break;

                if (max_pos_x_in_pixel < timeline_orig_pos_x) {
                    current_clip = (Clip*)current_clip->next;
                    continue;
                }

                // Setup clip's minimum and maximum bounding box
                ImVec2 min_bb(min_pos_x_in_pixel, track_pos_y);
                ImVec2 max_bb(max_pos_x_in_pixel, track_pos_y + track->height);
                ImVec4 fine_scissor_rect(min_bb.x, min_bb.y, max_bb.x, max_bb.y);
                bool hovering_left_side = false;
                bool hovering_right_side = false;

                if (hovering_current_track && clip_action == GUITimelineClipAction::None) {
                    ImRect clip_rect(min_bb, max_bb);
                    // Sizing hitboxes
                    ImRect lhs(min_pos_x_in_pixel, track_pos_y, min_pos_x_in_pixel + 4.0f, max_bb.y);
                    ImRect rhs(max_pos_x_in_pixel - 4.0f, track_pos_y, max_pos_x_in_pixel, max_bb.y);

                    // Start a clip action in the next frame
                    if (lhs.Contains(mouse_pos)) {
                        if (left_mouse_clicked)
                            clip_action = GUITimelineClipAction::ResizeLeft;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        hovering_left_side = true;
                    }
                    else if (rhs.Contains(mouse_pos)) {
                        if (left_mouse_clicked)
                            clip_action = GUITimelineClipAction::ResizeRight;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        hovering_right_side = true;
                    }
                    else if (clip_rect.Contains(mouse_pos)) {
                        if (left_mouse_clicked)
                            clip_action = !ImGui::IsKeyDown(ImGuiKey_LeftShift) ?
                                GUITimelineClipAction::Move :
                                GUITimelineClipAction::Duplicate;
                        else if (right_mouse_clicked)
                            clip_action = GUITimelineClipAction::ContextMenu;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }

                    if (clip_action != GUITimelineClipAction::None) {
                        initial_move_pos = mouse_pos_time_grid;
                        g_selected_track = track.get();
                        g_selected_clip = current_clip;
                    }
                }

                static constexpr float border_contrast_ratio = 1.0f / 3.5f;
                static constexpr float text_contrast_ratio = 1.0f / 1.57f;
                float bg_contrast_ratio = calc_contrast_ratio(current_clip->color, text_color);
                ImColor border_color = (bg_contrast_ratio > border_contrast_ratio) ? ImColor(0.0f, 0.0f, 0.0f, 0.3f) : ImColor(1.0f, 1.0f, 1.0f, 0.2f);
                ImColor intended_text_color = (bg_contrast_ratio > text_contrast_ratio) ? ImColor(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.6f) : text_color;

                // Limit the number of peaks that should be drawn 
                double dist_from_start = std::max(scroll_pos_x - min_pos_x, 0.0);
                double dist_to_end = std::min(((double)timeline_width + scroll_pos_x) - min_pos_x, max_pos_x - min_pos_x);
                uint32_t start_sample = (uint32_t)std::floor(dist_from_start * inv_sample_scale);
                uint32_t end_sample = (uint32_t)std::ceil(dist_to_end * inv_sample_scale);

                // Draw clip elements.
                float clip_title_max_y = min_bb.y + font_size + 2.0f; 
                ImVec2 clip_title_max_bb = ImVec2(max_bb.x, clip_title_max_y);
                ImVec2 clip_content_min = ImVec2(min_bb.x, clip_title_max_y);
                draw_list->Flags = disable_aa;
                draw_list->AddRectFilled(min_bb, clip_title_max_bb, current_clip->color);
                draw_list->AddRectFilled(clip_content_min, max_bb, color_adjust_alpha(track->color, 0.35f));
                draw_list->AddRect(min_bb, clip_title_max_bb, border_color);
                draw_list->Flags = old_draw_list;

                const char* str = current_clip->name.c_str();
                ImVec4 clip_label_rect(min_bb.x, min_bb.y, max_bb.x - 6.0f, clip_title_max_y);
                draw_list->AddText(font, font_size, ImVec2(std::max(min_bb.x, timeline_orig_pos_x) + 3.0f, min_bb.y),
                                   intended_text_color, str, str + current_clip->name.size(),
                                   0.0f, &clip_label_rect);

                // Push which content needs to be drawn.
                const AudioClip* audio_clip = static_cast<AudioClip*>(current_clip);
                SamplePeaks* sample_peaks = audio_clip->asset.ref->peaks.get();
                clip_content_draw_list.push_back(
                    {
                        .sample_peaks = sample_peaks,
                        .color = color_brighten(current_clip->color, 0.85f),
                        .min = clip_content_min,
                        .max = max_bb,
                        .scale_x = (float)sample_scale,
                        .start_sample_idx = start_sample,
                        .end_sample_idx = std::min(end_sample, sample_peaks->sample_count),
                    });

                // TODO: Move this outside loop.
                if (hovering_left_side)
                    draw_list->AddLine(ImVec2(min_bb.x + 1.0f, min_bb.y),
                                       ImVec2(min_bb.x + 1.0f, max_bb.y),
                                       ImGui::GetColorU32(ImGuiCol_SeparatorHovered), 3.0f);

                if (hovering_right_side)
                    draw_list->AddLine(ImVec2(max_bb.x - 2.0f, min_bb.y),
                                       ImVec2(max_bb.x - 2.0f, max_bb.y),
                                       ImGui::GetColorU32(ImGuiCol_SeparatorHovered), 3.0f);

                current_clip = (Clip*)current_clip->next;
            }

#if 0
            for (auto& msg : track->dbg_message) {
                double pos = seconds_to_beat((double)(msg.sample_position) / 44100.0, beat_duration);
                draw_list->AddLine(ImVec2(timeline_scroll_offset_x + (float)(pos * clip_scale), track_pos_y),
                                   ImVec2(timeline_scroll_offset_x + (float)(pos * clip_scale), track_pos_y + track->height), ImColor(0, 255, 0));
            }
#endif

            track_pos_y += track->height;
            
            draw_list->AddLine(ImVec2(timeline_orig_pos_x, track_pos_y + 0.5f),
                               ImVec2(timeline_orig_pos_x + timeline_width, track_pos_y + 0.5f),
                               track_separator_color, 2.0f);

            track_pos_y += 2.0f;
        }

        // Merge clip content from the offscreen framebuffer
        ImVec2 min_bb;
        ImTextureID clip_content_fb_tex = clip_content_fb->get_imgui_texture_id();
        ImVec2 uv_timeline_area(1.0f / timeline_area.x, 1.0f / timeline_area.y); // Normalized UV
        draw_list->PushTextureID(clip_content_fb_tex);
        for (auto& clip_content : clip_content_draw_list) {
            ImVec2 fb_min(clip_content.min.x - timeline_orig_pos_x_rounded, clip_content.min.y - scroll_offset_y);
            ImVec2 fb_max(clip_content.max.x - timeline_orig_pos_x_rounded, clip_content.max.y - scroll_offset_y);
            draw_list->AddImage(clip_content_fb_tex, clip_content.min, clip_content.max,
                                ImVec2(fb_min.x * uv_timeline_area.x, fb_min.y * uv_timeline_area.y),
                                ImVec2(fb_max.x * uv_timeline_area.x, fb_max.y * uv_timeline_area.y));
            clip_content.min = fb_min;
            clip_content.max = fb_max;
        }
        draw_list->PopTextureID();

        static bool use_aa = true;

        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            use_aa = !use_aa;
            should_redraw_clip_content = true;
        }
      
        // Render clip content to an offscreen framebuffer
        if (clip_content_draw_list.size() > 0 && should_redraw_clip_content) {
            Renderer* renderer = Renderer::instance;
            renderer->set_framebuffer(clip_content_fb);
            renderer->clear_framebuffer(0.0f, 0.0f, 0.0f, 0.0f);
            renderer->draw_clip_content(clip_content_draw_list, use_aa);
        }

        // Draw playhead line
        if (g_engine.is_playing()) {
            float playhead_pos = std::round(timeline_orig_pos_x - scroll_pos_x + map_playhead_to_screen_position(view_scale, playhead_position));
            draw_list->AddLine(ImVec2(playhead_pos, scroll_offset_y),
                               ImVec2(playhead_pos, scroll_offset_y + timeline_area.y),
                               playhead_color);
        }

        ImGui::PopClipRect();

        // Draw Separator
        ImU32 separator_color = (is_separator_active || is_separator_hovered) ?
            ImGui::GetColorU32(ImGuiCol_SeparatorHovered) :
            ImGui::GetColorU32(ImGuiCol_Separator);
        draw_list->AddLine(ImVec2(draw_pos.x + clamped_separator + 0.5f, scroll_offset_y),
                           ImVec2(draw_pos.x + clamped_separator + 0.5f, scroll_offset_y + timeline_area.y),
                           separator_color, 2.0f);

        // Handle zooming
        if (timeline_hovered && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && mouse_wheel != 0.0f) {
            do_zoom(mouse_pos.x, timeline_orig_pos_x, view_scale, mouse_wheel);
            zooming = true;
        }

        last_scroll_pos_y = ImGui::GetScrollY();
        render_clip_context_menu();
        
        ImGui::EndChild();
        ImGui::End();
    }

    void GUITimeline::do_horizontal_scroll_drag(float drag_delta, double scroll_view_width, double direction)
    {
        double norm_drag_delta = ((double)drag_delta / scroll_view_width) * direction;
        if (drag_delta != 0.0f) {
            double new_min_scroll_pos_x = min_scroll_pos_x + norm_drag_delta;
            double new_max_scroll_pos_x = max_scroll_pos_x + norm_drag_delta;
            if (new_min_scroll_pos_x >= 0.0f && new_max_scroll_pos_x <= 1.0f) {
                min_scroll_pos_x = new_min_scroll_pos_x;
                max_scroll_pos_x = new_max_scroll_pos_x;
            }
            else {
                // Clip overflowing scroll
                if (new_min_scroll_pos_x < 0.0f) {
                    min_scroll_pos_x = 0.0f;
                    max_scroll_pos_x = new_max_scroll_pos_x + std::abs(new_min_scroll_pos_x);
                }
                else if (new_max_scroll_pos_x > 1.0f) {
                    min_scroll_pos_x = new_min_scroll_pos_x - (new_max_scroll_pos_x - 1.0f);
                    max_scroll_pos_x = 1.0f;
                }
            }
            should_redraw_clip_content = true;
        }
    }

    void GUITimeline::do_zoom(float mouse_pos_x, float cursor_pos_x, double view_scale, float mouse_wheel)
    {
        // Get the mouse position in scroll bar coordinates
        float zoom_position = (float)((double)(mouse_pos_x - cursor_pos_x) / music_length * view_scale) + (float)min_scroll_pos_x;
        if (zoom_position <= 1.0f) {
            float dist_from_start = zoom_position - (float)min_scroll_pos_x;
            float dist_to_end = (float)max_scroll_pos_x - zoom_position;
            mouse_wheel *= 0.1f;
            min_scroll_pos_x = std::clamp((float)min_scroll_pos_x + dist_from_start * mouse_wheel, 0.0f, (float)max_scroll_pos_x);
            max_scroll_pos_x = std::clamp((float)max_scroll_pos_x - dist_to_end * mouse_wheel, (float)min_scroll_pos_x, 1.0f);
            should_redraw_clip_content = true;
        }
    }

    void GUITimeline::finish_clip_action()
    {
        g_selected_clip = nullptr;
        g_selected_track = nullptr;
        clip_action = GUITimelineClipAction::None;
        initial_move_pos = 0.0;
    }

    float GUITimeline::map_playhead_to_screen_position(double view_scale, double playhead_position)
    {
        return (float)(playhead_position * 96.0 / view_scale);
    }

    float GUITimeline::calculate_music_length()
    {
        return (float)music_length * 96.0f;
    }
}