#include "timeline_base.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "engine/engine.h"
#include "platform/platform.h"
#include <fmt/format.h>
#include <imgui.h>

namespace wb {
void TimelineBase::render_horizontal_scrollbar() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();
    float font_size = ImGui::GetFontSize();
    float btn_size_y = font_size + style.FramePadding.y * 2.0f;
    ImVec2 arrow_btn_size = ImGui::CalcItemSize(ImVec2(), font_size + style.FramePadding.x * 2.0f, btn_size_y);
    ImGui::SetCursorPosX(math::max(separator_pos, min_track_control_size) + 2.0f);
    ImGui::PushButtonRepeat(true);

    if (ImGui::Button("<", arrow_btn_size))
        scroll_horizontal(-0.05f, 1.0f);

    // Calculate the scroll bar length
    auto scroll_btn_length = ImGui::GetContentRegionAvail().x - arrow_btn_size.x;
    ImGui::SameLine();
    auto scroll_btn_min_bb = ImGui::GetCursorScreenPos();
    ImGui::SameLine(scroll_btn_length);
    auto scroll_btn_max_bb = ImGui::GetCursorScreenPos();

    if (ImGui::Button(">", arrow_btn_size))
        scroll_horizontal(0.05f, 1.0f);

    ImGui::PopButtonRepeat();

    // Add gap between arrow buttons and the scroll grab
    scroll_btn_min_bb.x += 1.0f;
    scroll_btn_max_bb.x -= 1.0f;

    // Insert scroll bar button at the middle of arrow buttons
    auto scroll_btn_max_length = scroll_btn_max_bb.x - scroll_btn_min_bb.x;
    ImGui::SetCursorScreenPos(scroll_btn_min_bb);
    ImGui::InvisibleButton("##timeline_hscroll", ImVec2(scroll_btn_max_length, btn_size_y));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    bool scrolling = resizing_lhs_scroll_grab || resizing_rhs_scroll_grab || grabbing_scroll;

    if (scrolling)
        redraw = true;

    if (!active && scrolling) {
        resizing_lhs_scroll_grab = false;
        resizing_rhs_scroll_grab = false;
        grabbing_scroll = false;
        ImGui::ResetMouseDragDelta();
    }

    /*if (hovered && (ImGui::GetIO().MouseWheel != 0.0f)) {
        double view_scale = (max_hscroll - min_hscroll) * song_length / (double)area_size.x;
    }*/

    // Transform scroll units in pixels
    double min_space = 4.0 / scroll_btn_max_length;
    float min_hscroll_pixels = (float)std::round(math::min(min_hscroll, 1.0 - min_space) * scroll_btn_max_length);
    float max_hscroll_pixels = (float)std::round(math::min(max_hscroll, 1.0) * scroll_btn_max_length);
    float dist_pixel = max_hscroll_pixels - min_hscroll_pixels;

    if (dist_pixel < 4.0f) {
        max_hscroll_pixels = min_hscroll_pixels + 4.0f;
    }

    // Calculate bounds
    float lhs_x = scroll_btn_min_bb.x + min_hscroll_pixels;
    float rhs_x = scroll_btn_min_bb.x + max_hscroll_pixels;

    ImVec2 lhs_min(lhs_x, scroll_btn_min_bb.y);
    ImVec2 lhs_max(lhs_x + 2.0f, scroll_btn_min_bb.y + btn_size_y);
    ImVec2 rhs_min(rhs_x - 2.0f, scroll_btn_min_bb.y);
    ImVec2 rhs_max(rhs_x, scroll_btn_min_bb.y + btn_size_y);

    // Check whether the mouse hovering the left-hand side bound
    if (!scrolling && ImGui::IsMouseHoveringRect(lhs_min, lhs_max)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active && !resizing_lhs_scroll_grab) {
            last_hscroll = min_hscroll;
            resizing_lhs_scroll_grab = true;
        }
    }
    // Check whether the mouse hovering the right-hand side bound
    else if (!scrolling && ImGui::IsMouseHoveringRect(rhs_min, rhs_max)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active && !resizing_rhs_scroll_grab) {
            if (max_hscroll > 1.0) {
                max_hscroll -= max_hscroll - 1.0;
            }
            last_hscroll = max_hscroll;
            resizing_rhs_scroll_grab = true;
        }
    }
    // Check whether the mouse is grabbing the scroll
    else if (ImGui::IsMouseHoveringRect(lhs_min, rhs_max) && active && !scrolling) {
        last_hscroll = min_hscroll;
        grabbing_scroll = true;
    }
    // Check whether the mouse clicking on the scroll area
    else if (ImGui::IsItemActivated()) {
        double scroll_grab_length = max_hscroll - min_hscroll;
        double half_scroll_grab_length = scroll_grab_length * 0.5;
        auto mouse_pos_x = (ImGui::GetMousePos().x - scroll_btn_min_bb.x) / (double)scroll_btn_max_length;
        double new_min_hscroll = math::clamp(mouse_pos_x - half_scroll_grab_length, 0.0, 1.0 - scroll_grab_length);
        max_hscroll = new_min_hscroll + scroll_grab_length;
        min_hscroll = new_min_hscroll;
        redraw = true;
    }

    if (resizing_lhs_scroll_grab) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        min_hscroll = math::clamp(last_hscroll + drag_delta.x / scroll_btn_max_length, 0.0, max_hscroll - min_space);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (resizing_rhs_scroll_grab) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        max_hscroll = math::max(last_hscroll + drag_delta.x / scroll_btn_max_length, min_hscroll + min_space);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (grabbing_scroll) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta();
        double scroll_grab_length = max_hscroll - min_hscroll;
        double new_min_hscroll = math::max(last_hscroll + drag_delta.x / scroll_btn_max_length, 0.0);
        max_hscroll = new_min_hscroll + scroll_grab_length;
        min_hscroll = new_min_hscroll;
    }

    draw_list->AddRectFilled(lhs_min, rhs_max, ImGui::GetColorU32(ImGuiCol_Button), style.GrabRounding);
    if (hovered || active) {
        uint32_t color =
            active ? ImGui::GetColorU32(ImGuiCol_FrameBgActive) : ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
        draw_list->AddRect(lhs_min, rhs_max, color, style.GrabRounding);
    }
}

bool TimelineBase::render_time_ruler(double* time_value) {
    ImGuiStyle& style = ImGui::GetStyle();
    auto col = ImGui::GetColorU32(ImGuiCol_Separator, 1.0f);
    auto draw_list = ImGui::GetWindowDrawList();
    auto mouse_pos = ImGui::GetMousePos();

    ImGui::SetCursorPosX(math::max(separator_pos, min_track_control_size) + 2.0f);

    double view_scale = calc_view_scale();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    auto size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
    ImGui::InvisibleButton("##time_ruler_control", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    bool left_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool middle_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Middle);
    bool holding_left = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (timeline_width == 0.0f) {
        return false;
    }

    bool active = false;
    if (left_clicked || (holding_left && std::abs(drag_delta.x) > 0.001f)) {
        double mapped_x_pos = (double)(mouse_pos.x - cursor_pos.x) / song_length * view_scale + min_hscroll;
        double mouse_time_pos = mapped_x_pos * song_length * inv_ppq;
        double mouse_time_pos_grid = math::max(std::round(mouse_time_pos * grid_scale) / grid_scale, 0.0);
        *time_value = mouse_time_pos_grid;
        // g_engine.set_playhead_position(mouse_time_pos_grid);
        ImGui::ResetMouseDragDelta();
        active = true;
    }

    // Handle zoom scrolling on ruler
    float mouse_wheel = ImGui::GetIO().MouseWheel;
    if (hovered && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel * 0.25f);
        view_scale = calc_view_scale();
    }

    // Start zoom
    if (middle_clicked) {
        ImVec2 pos = ImGui::GetMousePos();
        zooming_on_ruler = true;
        GImGui->ColorPickerRef.x = pos.x;
        GImGui->ColorPickerRef.y = pos.y;
        // Reset relative mouse state to prevent jumping
        wm_set_mouse_pos((int)pos.x, (int)pos.y);
        wm_reset_relative_mouse_state();
        wm_enable_relative_mouse_mode(true);
    }

    if (zooming_on_ruler) {
        int x, y;
        wm_get_relative_mouse_state(&x, &y);
        if (y != 0) {
            zoom(mouse_pos.x, cursor_pos.x, view_scale, (float)y * 0.01f);
            view_scale = calc_view_scale();
        }
    }

    // Release zoom
    if (zooming_on_ruler && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        view_scale = calc_view_scale();
        zooming_on_ruler = false;
        wm_enable_relative_mouse_mode(false);
        wm_set_mouse_pos((int)GImGui->ColorPickerRef.x, (int)GImGui->ColorPickerRef.y);
    }

    ImFont* font = ImGui::GetFont();
    ImU32 time_point_color = ImGui::GetColorU32(ImGuiCol_Text);
    double division = math::max(std::exp2(math::round(std::log2(view_scale / 8.0))), 1.0);
    double inv_view_scale = 1.0 / view_scale;
    double bar = 4.0 * g_engine.ppq * inv_view_scale;
    float grid_inc_x = (float)(bar * division);
    float inv_grid_inc_x = 1.0f / grid_inc_x;
    float scroll_pos_x = (float)std::round((min_hscroll * song_length) * inv_view_scale);
    float gridline_pos_x = cursor_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
    float scroll_offset = cursor_pos.x - scroll_pos_x;
    int line_count = (uint32_t)(size.x * inv_grid_inc_x) + 1;
    int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);

    draw_list->PushClipRect(cursor_pos, ImVec2(cursor_pos.x + size.x, cursor_pos.y + size.y));

    bool is_playing = g_engine.is_playing();
    if (is_playing) {
        double playhead_start = g_engine.playhead_start * g_engine.ppq * inv_view_scale;
        float position = (float)std::round(scroll_offset + playhead_start) - size.y * 0.5f;
        draw_list->AddTriangleFilled(ImVec2(position, cursor_pos.y + 2.5f),
                                     ImVec2(position + size.y, cursor_pos.y + 2.5f),
                                     ImVec2(position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f), col);
    }

    // Draw tick
    float tick_pos_y = cursor_pos.y + size.y;
    uint32_t step = (uint32_t)division;
    for (int i = 0; i <= line_count; i++) {
        char digits[24] {};
        int bar_point = i + count_offset;
        float rounded_gridline_pos_x = std::round(gridline_pos_x);
        fmt::format_to_n(digits, sizeof(digits), "{}", bar_point * step + 1);
        draw_list->AddText(ImVec2(rounded_gridline_pos_x + 4.0f, cursor_pos.y + style.FramePadding.y * 2.0f - 2.0f),
                           time_point_color, digits);
        draw_list->AddLine(ImVec2(rounded_gridline_pos_x, tick_pos_y - 8.0f),
                           ImVec2(rounded_gridline_pos_x, tick_pos_y - 3.0f), col);
        gridline_pos_x += grid_inc_x;
    }

    float playhead_screen_position =
        (float)std::round(scroll_offset + playhead * g_engine.ppq * inv_view_scale) - size.y * 0.5f;
    draw_list->AddTriangleFilled(ImVec2(playhead_screen_position, cursor_pos.y + 2.5f),
                                 ImVec2(playhead_screen_position + size.y, cursor_pos.y + 2.5f),
                                 ImVec2(playhead_screen_position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f),
                                 playhead_color);

    draw_list->PopClipRect();

    return active;
}

void TimelineBase::scroll_horizontal(float drag_delta, double max_length, double direction) {
    double norm_drag_delta = ((double)drag_delta / max_length) * direction;
    if (drag_delta != 0.0f) {
        double new_min_hscroll = min_hscroll + norm_drag_delta;
        double new_max_hscroll = max_hscroll + norm_drag_delta;
        if (new_min_hscroll >= 0.0f) {
            min_hscroll = new_min_hscroll;
            max_hscroll = new_max_hscroll;
        } else {
            // Clamp
            if (new_min_hscroll < 0.0) {
                min_hscroll = 0.0;
                max_hscroll = new_max_hscroll + std::abs(new_min_hscroll);
            }
        }
        redraw = true;
    }
}

void TimelineBase::zoom(float mouse_pos_x, float cursor_pos_x, double view_scale, float mouse_wheel) {
    if (max_hscroll > 1.0) {
        double dist = max_hscroll - 1.0;
        min_hscroll -= dist;
        max_hscroll -= dist;
    }

    double zoom_position = ((double)(mouse_pos_x - cursor_pos_x) / song_length * view_scale) + min_hscroll;
    double dist_from_start = zoom_position - min_hscroll;
    double dist_to_end = max_hscroll - zoom_position;
    min_hscroll = math::clamp(min_hscroll + dist_from_start * (double)mouse_wheel, 0.0, max_hscroll);
    max_hscroll = math::clamp(max_hscroll - dist_to_end * (double)mouse_wheel, min_hscroll, 1.0);
    redraw = true;
}
} // namespace wb