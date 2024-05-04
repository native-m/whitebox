#include "timeline.h"
#include "browser.h"
#include "controls.h"
#include "core/color.h"
#include "core/debug.h"
#include "core/math.h"
#include "engine/engine.h"
#include "forms.h"
#include "layout.h"
#include "popup_state_manager.h"
#include <fmt/format.h>

namespace wb {

inline static void draw_clip(ImDrawList* draw_list, ImVector<ClipContentDrawCmd>& clip_content_cmds,
                             const Track* track, const Clip* clip, float min_pos_x, float offset_y,
                             double timeline_width, double sample_scale, double clip_scale,
                             double start_sample_pos, const ImVec2& min_bb, const ImVec2& max_bb,
                             const ImColor& text_color, ImFont* font, float font_size) {
    constexpr ImDrawListFlags draw_list_aa_flags = ImDrawListFlags_AntiAliasedFill |
                                                   ImDrawListFlags_AntiAliasedLinesUseTex |
                                                   ImDrawListFlags_AntiAliasedLines;

    // Color accessibility adjustments...
    static constexpr float border_contrast_ratio = 1.0f / 3.5f;
    static constexpr float text_contrast_ratio = 1.0f / 2.0f;
    float bg_contrast_ratio = calc_contrast_ratio(clip->color, text_color);
    ImColor border_color = (bg_contrast_ratio > border_contrast_ratio)
                               ? ImColor(0.0f, 0.0f, 0.0f, 0.3f)
                               : ImColor(1.0f, 1.0f, 1.0f, 0.2f);
    ImColor text_color_adjusted = (bg_contrast_ratio > text_contrast_ratio)
                                      ? ImColor(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.45f)
                                      : text_color;

    // Draw clip background and its header
    float clip_title_max_y = min_bb.y + font_size + 4.0f;
    ImVec2 clip_title_max_bb = ImVec2(max_bb.x, clip_title_max_y);
    ImVec2 clip_content_min = ImVec2(min_bb.x, clip_title_max_y);
    ImDrawListFlags tmp_flags = draw_list->Flags;
    draw_list->Flags = draw_list->Flags & ~draw_list_aa_flags;
    draw_list->AddRectFilled(min_bb, clip_title_max_bb, clip->color);
    draw_list->AddRectFilled(clip_content_min, max_bb, track->color); // color_adjust_alpha(track->color, 0.35f)
    draw_list->AddRect(min_bb, clip_title_max_bb, border_color);
    draw_list->Flags = tmp_flags;

    // Draw clip label
    const char* str = clip->name.c_str();
    ImVec4 clip_label_rect(min_bb.x, min_bb.y, max_bb.x - 6.0f, clip_title_max_y);
    draw_list->AddText(font, font_size,
                       ImVec2(std::max(min_bb.x, min_pos_x) + 3.0f, min_bb.y + 2.0f),
                       text_color_adjusted, str, str + clip->name.size(), 0.0f, &clip_label_rect);

    SampleAsset* asset = clip->audio.asset;
    static double log_base4 = 1.0 / 1.3862943611198906; // 1.0 / log(4.0)
    if (asset) {
        SamplePeaks* sample_peaks = asset->peaks.get();

        // This kinda bad
        double scale_x = sample_scale * (double)asset->sample_instance.sample_rate;
        double inv_scale_x = 1.0 / scale_x;
        double mip_index = std::log(scale_x * 0.5) * log_base4; // Scale -> Index
        uint32_t index = std::clamp((uint32_t)mip_index, 0u, sample_peaks->mipmap_count);
        double mip_scale = std::pow(4.0, (mip_index - (double)index)) * 2.0; // Index -> Mip Scale

        ImVec2 fb_min(clip_content_min.x - min_pos_x, clip_content_min.y - offset_y);
        ImVec2 fb_max(max_bb.x - min_pos_x, max_bb.y - offset_y);
        float min_rem = std::fmod(fb_min.x, 2.0f) * 0.5;
        double waveform_start = std::max(start_sample_pos * inv_scale_x, 0.0);
        double waveform_end = ((double)sample_peaks->sample_count - start_sample_pos) * inv_scale_x;
        float min_x = std::max(fb_min.x, 0.0f);
        float max_x =
            (float)std::min(std::min((double)fb_max.x, fb_min.x + waveform_end), timeline_width);
        uint32_t draw_count = (uint32_t)(max_x - min_x);
        uint32_t start_idx =
            (uint32_t)std::round(std::max(-fb_min.x, 0.0f) + (float)waveform_start);

        //Log::info("{} {} {} {} {}", min_x, max_x, min_rem, start_idx, waveform_start);

        if (draw_count) {
            clip_content_cmds.push_back({
                .peaks = sample_peaks,
                .min_bb = ImVec2(min_x, fb_min.y),
                .max_bb = fb_max,
                .color = color_brighten(clip->color, 0.85f),
                .scale_x = (float)mip_scale,
                .mip_index = index,
                .start_idx = start_idx,
                .draw_count = draw_count * 2,
            });
        }
    }
}

inline void draw_clip2(ImDrawList* draw_list, ImVector<ClipContentDrawCmd>& clip_content_cmds,
                       const Clip* clip, float timeline_width, float offset_y, float min_draw_x,
                       double min_x, double max_x, double clip_scale, double sample_scale,
                       double start_sample_pos, float track_pos_y, float track_height,
                       const ImColor& track_color, ImFont* font, const ImColor& text_color) {
    constexpr ImDrawListFlags draw_list_aa_flags = ImDrawListFlags_AntiAliasedFill |
                                                   ImDrawListFlags_AntiAliasedLinesUseTex |
                                                   ImDrawListFlags_AntiAliasedLines;

    // Color accessibility adjustments...
    static constexpr float border_contrast_ratio = 1.0f / 3.5f;
    static constexpr float text_contrast_ratio = 1.0f / 2.0f;
    float bg_contrast_ratio = calc_contrast_ratio(clip->color, text_color);
    ImColor border_color = (bg_contrast_ratio > border_contrast_ratio)
                               ? ImColor(0.0f, 0.0f, 0.0f, 0.3f)
                               : ImColor(1.0f, 1.0f, 1.0f, 0.2f);
    ImColor text_color_adjusted = (bg_contrast_ratio > text_contrast_ratio)
                                      ? ImColor(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.45f)
                                      : text_color;

    // Draw clip background and its header
    float min_x2 = (float)std::round(min_x);
    float max_x2 = (float)std::round(max_x);
    float font_size = font->FontSize;
    float clip_title_max_y = track_pos_y + font_size + 4.0f;
    ImVec2 clip_title_min_bb = ImVec2(min_x2, track_pos_y);
    ImVec2 clip_title_max_bb = ImVec2(max_x2, clip_title_max_y);
    ImVec2 clip_content_min = ImVec2(min_x2, clip_title_max_y);
    ImVec2 clip_content_max = ImVec2(max_x2, track_pos_y + track_height);
    ImDrawListFlags tmp_flags = draw_list->Flags;
    draw_list->Flags = draw_list->Flags & ~draw_list_aa_flags;
    draw_list->AddRectFilled(clip_title_min_bb, clip_title_max_bb, clip->color);
    draw_list->AddRectFilled(clip_content_min, clip_content_max,
        color_adjust_alpha(track_color, 0.35f));
    draw_list->AddRect(clip_title_min_bb, clip_title_max_bb, border_color);
    draw_list->Flags = tmp_flags;

    // Draw clip label
    const char* str = clip->name.c_str();
    ImVec4 clip_label_rect(clip_title_min_bb.x, clip_title_min_bb.y, clip_title_max_bb.x - 6.0f,
                           clip_title_max_y);
    draw_list->AddText(font, font_size,
                       ImVec2(std::max(clip_title_min_bb.x, min_draw_x) + 3.0f, track_pos_y + 2.0f),
                       text_color_adjusted, str, str + clip->name.size(), 0.0f, &clip_label_rect);

    SampleAsset* asset = clip->audio.asset;
    static constexpr double log_base4 = 1.0 / 1.3862943611198906; // 1.0 / log(4.0)
    if (asset) {
        SamplePeaks* sample_peaks = asset->peaks.get();

        // This kinda bad
        double scale_x = sample_scale * (double)asset->sample_instance.sample_rate;
        double inv_scale_x = 1.0 / scale_x;
        double mip_index = std::log(scale_x * 0.5) * log_base4; // Scale -> Index
        uint32_t index = std::clamp((uint32_t)mip_index, 0u, sample_peaks->mipmap_count);
        double mip_scale = std::pow(4.0, (mip_index - (double)index)) * 2.0; // Index -> Mip Scale

        double waveform_start = start_sample_pos * inv_scale_x;
        double waveform_len = ((double)asset->sample_instance.count - waveform_start) * inv_scale_x;
        double rel_min_x = min_x - (double)min_draw_x;
        double rel_max_x = max_x - (double)min_draw_x;
        double min_pos_x = std::max(rel_min_x, 0.0);
        double max_pos_x =
            std::min(std::min(rel_max_x, rel_min_x + waveform_len), (double)(timeline_width + 2.0));
        double draw_count = std::max(max_pos_x - min_pos_x, 0.0);
        double start_idx = std::max(-rel_min_x, 0.0) + waveform_start;

        //Log::info("{} {} {}", min_pos_x - waveform_start, draw_count, start_idx);

        if (draw_count) {
            clip_content_cmds.push_back({
                .peaks = sample_peaks,
                .min_bb = ImVec2((float)std::round(min_pos_x), clip_content_min.y - offset_y),
                .max_bb = ImVec2((float)std::round(max_pos_x), clip_content_max.y - offset_y),
                .color = color_brighten(clip->color, 0.85f),
                .scale_x = (float)mip_scale,
                .mip_index = index,
                .start_idx = (uint32_t)std::round(start_idx),
                .draw_count = (uint32_t)draw_count * 2,
            });
        }
    }
}

void GuiTimeline::init() {
    priv_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
    g_engine.add_on_bpm_change_listener(
        [this](double bpm, double beat_duration) { force_redraw = true; });
}

void GuiTimeline::shutdown() {
    delete priv_draw_list;
    timeline_fb.reset();
}

Track* GuiTimeline::add_track() {
    g_engine.edit_lock();
    auto track = g_engine.add_track("New Track");
    track->color = ImColor::HSV((float)color_spin / 15, 0.5f, 0.7f);
    color_spin = (color_spin + 1) % 15;
    g_engine.edit_unlock();
    redraw = true;
    return track;
}

void GuiTimeline::render() {
    constexpr ImGuiWindowFlags timeline_content_area_flags =
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar;

    if (!open)
        return;

    playhead = g_engine.playhead_ui.load(std::memory_order_relaxed);
    inv_ppq = 1.0 / g_engine.ppq;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
    if (!controls::begin_dockable_window("Timeline", &open)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    ImGui::PopStyleVar();

    redraw = force_redraw;
    if (force_redraw)
        force_redraw = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    render_horizontal_scrollbar();
    render_time_ruler();
    ImGui::PopStyleVar();

    ImVec2 content_origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(content_origin.x, content_origin.y - 1.0f),
        ImVec2(content_origin.x + ImGui::GetContentRegionAvail().x, content_origin.y - 1.0f),
        ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::BeginChild("##timeline_content", ImVec2(), 0, timeline_content_area_flags);

    win_draw_list = ImGui::GetWindowDrawList();
    content_min = ImGui::GetWindowContentRegionMin();
    content_max = ImGui::GetWindowContentRegionMax();
    area_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);
    vscroll = ImGui::GetScrollY();

    ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
    if (scrolling && scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
        ImGui::SetScrollY(vscroll - scroll_delta_y);
        redraw = true;
    }

    if ((last_vscroll - vscroll) != 0.0f)
        redraw = true;

    render_separator();
    render_track_controls();
    render_tracks();

    ImGui::EndChild();
    ImGui::End();
}

inline void GuiTimeline::render_horizontal_scrollbar() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();
    float font_size = ImGui::GetFontSize();
    float btn_size_y = font_size + style.FramePadding.y * 2.0f;
    ImVec2 arrow_btn_size =
        ImGui::CalcItemSize(ImVec2(), font_size + style.FramePadding.x * 2.0f, btn_size_y);
    ImGui::SetCursorPosX(std::max(separator_pos, 100.0f) + 2.0f);
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

    if (hovered && (ImGui::GetIO().MouseWheel != 0.0f)) {
        double view_scale = (max_hscroll - min_hscroll) * song_length / (double)area_size.x;
    }

    // Transform scroll units in pixels
    float min_hscroll_pixels = (float)min_hscroll * scroll_btn_max_length;
    float max_hscroll_pixels = (1.0f - (float)max_hscroll) * scroll_btn_max_length;

    // Calculate bounds
    float lhs_x = scroll_btn_min_bb.x + min_hscroll_pixels;
    float rhs_x = scroll_btn_max_bb.x - max_hscroll_pixels;
    ImVec2 lhs_min(lhs_x, scroll_btn_min_bb.y);
    ImVec2 lhs_max(lhs_x + 2.0f, scroll_btn_min_bb.y + btn_size_y);
    ImVec2 rhs_min(rhs_x - 2.0f, scroll_btn_min_bb.y);
    ImVec2 rhs_max(rhs_x, scroll_btn_min_bb.y + btn_size_y);

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
        last_hscroll = min_hscroll;
        grabbing_scroll = true;
    }
    // Check whether the mouse clicking on the scroll area
    else if (ImGui::IsItemActivated()) {
        double scroll_grab_length = max_hscroll - min_hscroll;
        double half_scroll_grab_length = scroll_grab_length * 0.5;
        auto mouse_pos_x =
            (ImGui::GetMousePos().x - scroll_btn_min_bb.x) / (double)scroll_btn_max_length;
        double new_min_hscroll =
            std::clamp(mouse_pos_x - half_scroll_grab_length, 0.0, 1.0 - scroll_grab_length);
        max_hscroll = new_min_hscroll + scroll_grab_length;
        min_hscroll = new_min_hscroll;
        redraw = true;
    }

    if (resizing_lhs_scroll_grab) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        min_hscroll =
            std::clamp(min_hscroll + drag_delta.x / scroll_btn_max_length, 0.0, max_hscroll);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    } else if (resizing_rhs_scroll_grab) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        max_hscroll =
            std::clamp(max_hscroll + drag_delta.x / scroll_btn_max_length, min_hscroll, 1.0);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    } else if (grabbing_scroll) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta();
        double scroll_grab_length = max_hscroll - min_hscroll;
        double new_min_hscroll = std::clamp(last_hscroll + drag_delta.x / scroll_btn_max_length,
                                            0.0, 1.0 - scroll_grab_length);
        max_hscroll = new_min_hscroll + scroll_grab_length;
        min_hscroll = new_min_hscroll;
    }

    draw_list->AddRectFilled(lhs_min, rhs_max, ImGui::GetColorU32(ImGuiCol_Button),
                             style.GrabRounding);
    if (hovered || active) {
        uint32_t color = active ? ImGui::GetColorU32(ImGuiCol_FrameBgActive)
                                : ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
        draw_list->AddRect(lhs_min, rhs_max, color, style.GrabRounding);
    }
}

inline void GuiTimeline::render_time_ruler() {
    ImGuiStyle& style = ImGui::GetStyle();
    auto col = ImGui::GetColorU32(ImGuiCol_Separator, 1.0f);
    auto draw_list = ImGui::GetWindowDrawList();
    auto mouse_pos = ImGui::GetMousePos();

    ImGui::SetCursorPosX(std::max(separator_pos, 100.0f) + 2.0f);

    auto size = ImVec2(ImGui::GetContentRegionAvail().x,
                       ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
    double view_scale = calc_view_scale();
    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##time_ruler_control", size);

    if (ImGui::IsItemActivated() || (ImGui::IsItemActive() && std::abs(drag_delta.x) > 0.001f)) {
        double mapped_x_pos =
            (double)(mouse_pos.x - cursor_pos.x) / song_length * view_scale + min_hscroll;
        double mouse_time_pos = mapped_x_pos * song_length * inv_ppq;
        double mouse_time_pos_grid =
            std::max(std::round(mouse_time_pos * grid_scale) / grid_scale, 0.0);
        // g_engine.set_play_position(mouse_time_pos_grid);
        ImGui::ResetMouseDragDelta();
    }

    // Handle zoom scrolling on ruler
    float mouse_wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsItemHovered() && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, cursor_pos.x, view_scale, mouse_wheel);
        view_scale = calc_view_scale();
    }

    // Assign zoom
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        zooming_on_ruler = true;

    if (zooming_on_ruler) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        zoom(mouse_pos.x, cursor_pos.x, view_scale, drag_delta.y * 0.1f);
        view_scale = calc_view_scale();
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    // Release zoom
    if (zooming_on_ruler && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        view_scale = calc_view_scale();
        zooming_on_ruler = false;
    }

    ImFont* font = ImGui::GetFont();
    float grid_inc_x = (float)g_engine.ppq * 4.0f / (float)view_scale;
    float inv_grid_inc_x = 1.0f / grid_inc_x;
    float scroll_pos_x = (float)(min_hscroll * song_length) / (float)view_scale;
    float gridline_pos_x = cursor_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
    float scroll_offset = cursor_pos.x - scroll_pos_x;
    int line_count = (uint32_t)(size.x * inv_grid_inc_x) + 1;
    int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);

    draw_list->PushClipRect(cursor_pos, ImVec2(cursor_pos.x + size.x, cursor_pos.y + size.y));

    /*bool is_playing = g_engine.is_playing();
    if (is_playing) {
        float play_position = std::round(scroll_offset + map_playhead_to_screen_position(
                                                             view_scale, g_engine.play_time)) -
                              size.y * 0.5f;
        draw_list->AddTriangleFilled(
            ImVec2(play_position, cursor_pos.y + 2.5f),
            ImVec2(play_position + size.y, cursor_pos.y + 2.5f),
            ImVec2(play_position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f), col);
    }*/

    for (int i = 0; i <= line_count; i++) {
        char digits[24] {};
        float rounded_gridline_pos_x = std::round(gridline_pos_x);
        fmt::format_to_n(digits, sizeof(digits), "{}", i + count_offset);
        draw_list->AddText(ImVec2(rounded_gridline_pos_x + 4.0f,
                                  cursor_pos.y + style.FramePadding.y * 2.0f - 2.0f),
                           ImGui::GetColorU32(ImGuiCol_Text), digits);
        draw_list->AddLine(ImVec2(rounded_gridline_pos_x, cursor_pos.y + size.y - 8.0f),
                           ImVec2(rounded_gridline_pos_x, cursor_pos.y + size.y - 3.0f), col);
        gridline_pos_x += grid_inc_x;
    }

    /*float playhead_screen_position =
        std::round(scroll_offset + map_playhead_to_screen_position(view_scale, playhead_position)) -
        size.y * 0.5f;
    draw_list->AddTriangleFilled(
        ImVec2(playhead_screen_position, cursor_pos.y + 2.5f),
        ImVec2(playhead_screen_position + size.y, cursor_pos.y + 2.5f),
        ImVec2(playhead_screen_position + size.y * 0.5f, cursor_pos.y + size.y - 2.5f),
        playhead_color);*/

    draw_list->PopClipRect();
}

inline void GuiTimeline::render_separator() {
    ImVec2 content_min = ImGui::GetWindowContentRegionMin();
    ImVec2 content_max = ImGui::GetWindowContentRegionMax();
    ImVec2 area_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);

    Layout layout;
    ImVec2 pos = layout.next(LayoutPosition::Fixed, ImVec2(separator_pos - 2.0f, 0.0f));

    ImGui::InvisibleButton("##timeline_separator", ImVec2(4, area_size.y));
    bool is_separator_active = ImGui::IsItemActive();
    bool is_separator_hovered = ImGui::IsItemHovered();

    if (is_separator_hovered || is_separator_active) {
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            separator_pos = 150.0f;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (is_separator_active) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        separator_pos += drag_delta.x;
        redraw = true;
    } else {
        separator_pos = std::max(separator_pos, 100.0f);
    }

    float clamped_separator_pos = std::max(separator_pos, 100.0f); // Limit separator to 100px
    float separator_x = layout.main_pos.x + clamped_separator_pos + 0.5f;
    /*ImU32 separator_color = (is_separator_hovered || is_separator_active)
                                ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                                : ImGui::GetColorU32(ImGuiCol_Separator);*/
    win_draw_list->AddLine(ImVec2(separator_x, pos.y), ImVec2(separator_x, pos.y + area_size.y),
                           ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

    layout.end();

    timeline_view_pos.x = layout.main_pos.x + clamped_separator_pos + 2.0f;
    timeline_view_pos.y = layout.main_pos.y;
}

inline void GuiTimeline::render_track_controls() {
    constexpr ImGuiWindowFlags track_control_window_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding;

    static constexpr float track_color_width = 8.0f;
    float clamped_separator_pos = std::max(separator_pos, 100.0f);
    ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const auto& style = ImGui::GetStyle();

    ImGui::PushClipRect(
        screen_pos,
        ImVec2(screen_pos.x + clamped_separator_pos, screen_pos.y + area_size.y + vscroll), true);

    int id = 0;
    for (auto& track : g_engine.tracks) {
        ImVec2 tmp_item_spacing = style.ItemSpacing;
        ImVec2 track_color_min = ImGui::GetCursorScreenPos();
        ImVec2 track_color_max =
            ImVec2(track_color_min.x + track_color_width, track_color_min.y + track->height);

        // Draw track color
        if (ImGui::IsRectVisible(track_color_min, track_color_max)) {
            win_draw_list->AddRectFilled(track_color_min, track_color_max,
                                         ImGui::GetColorU32((ImVec4)track->color));
        }

        ImGui::Indent(track_color_width);
        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(track_color_width - 2.0f, 2.0f));
        ImGui::BeginChild("##track_control",
                          ImVec2(clamped_separator_pos - track_color_width, track->height), false,
                          track_control_window_flags);

        {
            ImGui::PopStyleVar();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
            controls::collapse_button("##track_collapse", &track->shown);
            ImGui::PopStyleVar();

            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text((const char*)track->name.c_str());
            ImGui::DragFloat("Vol.", &track->volume, 0.01f, 0.0f, 0.0f, "%.2f db");

            ImGui::SmallButton("M");
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::SmallButton("S");

            if (ImGui::IsWindowHovered() &&
                !(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("track_context_menu");
                context_menu_track = track;
                tmp_color = track->color;
                tmp_name = track->name;
            }

            track_context_menu(*track, id);
            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
        ImGui::PopID();
        ImGui::Unindent(track_color_width);

        if (controls::hseparator_resizer(id, &track->height, 60.0f, 30.f, 500.f))
            redraw = true;

        ImGui::PopStyleVar();

        id++;
    }

    // ImGui::Text("%f %f", timeline_width, area_size.y);

    if (ImGui::Button("Add Track")) {
        add_track();
    }

    // ImGui::InvisibleButton("##test", ImVec2(10.0f, 1000.0f));

    //Log::info("{} {}", screen_pos.x, screen_pos.y + area_size.y + vscroll);

    ImGui::PopClipRect();
}

inline void GuiTimeline::track_context_menu(Track& track, int track_id) {
    if (ImGui::BeginPopup("track_context_menu")) {
        ImGui::MenuItem(track.name.c_str(), nullptr, false, false);
        ImGui::Separator();

        if (ImGui::BeginMenu("Rename")) {
            FormResult result = rename_form(&context_menu_track->name, &tmp_name);
            if (result == FormResult::Close)
                ImGui::CloseCurrentPopup();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Change Color")) {
            FormResult result = color_picker_form(&context_menu_track->color, tmp_color);
            switch (result) {
                case FormResult::ValueChanged:
                    force_redraw = true;
                    break;
                case FormResult::Close:
                    ImGui::CloseCurrentPopup();
                    force_redraw = true;
                    break;
            }
            ImGui::EndMenu();
        }

        ImGui::MenuItem("Duplicate");

        if (ImGui::MenuItem("Delete")) {
            g_engine.edit_lock();
            g_engine.delete_track((uint32_t)track_id);
            g_engine.edit_unlock();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset Height")) {
            ImGui::CloseCurrentPopup();
            redraw = true;
            track.height = 56.0f;
        }
        ImGui::EndPopup();
    }
}

inline void GuiTimeline::clip_context_menu() {
    if (ImGui::BeginPopup("clip_context_menu")) {
        if (ImGui::BeginMenu("Rename")) {
            FormResult result = rename_form(&context_menu_clip->name, &tmp_name);
            switch (result) {
                case FormResult::ValueChanged:
                    force_redraw = true;
                    break;
                case FormResult::Close:
                    ImGui::CloseCurrentPopup();
                    force_redraw = true;
                    break;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Change Color")) {
            FormResult result = color_picker_form(&context_menu_clip->color, tmp_color);
            switch (result) {
                case FormResult::ValueChanged:
                    force_redraw = true;
                    break;
                case FormResult::Close:
                    ImGui::CloseCurrentPopup();
                    force_redraw = true;
                    break;
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Delete")) {
            g_engine.edit_lock();
            context_menu_track->delete_clip(context_menu_clip->id);
            g_engine.edit_unlock();
            force_redraw = true;
        }

        if (ImGui::MenuItem("Duplicate")) {
            // TODO
            force_redraw = true;
        }

        ImGui::EndPopup();
    }
}

void GuiTimeline::render_tracks() {
    double ppq = g_engine.ppq;
    double beat_duration = g_engine.beat_duration.load(std::memory_order_relaxed);
    float offset_y = vscroll + timeline_view_pos.y;
    ImGui::SetCursorScreenPos(ImVec2(timeline_view_pos.x, timeline_view_pos.y));

    auto timeline_area = ImGui::GetContentRegionAvail();
    timeline_width = timeline_area.x;
    double timeline_width_f64 = (float)timeline_width;

    ImVec2 view_min(timeline_view_pos.x, offset_y);
    ImVec2 view_max(timeline_view_pos.x + timeline_width, offset_y + area_size.y);
    ImGui::PushClipRect(view_min, view_max, true);

    if (timeline_width != old_timeline_size.x || area_size.y != old_timeline_size.y) {
        // TODO: Limit resizing rate
        Log::info("Resizing timeline framebuffer ({}x{})", (int)timeline_width, (int)area_size.y);
        timeline_fb = g_renderer->create_framebuffer((int)timeline_width, (int)area_size.y);
        old_timeline_size.x = timeline_width;
        old_timeline_size.y = area_size.y;
        redraw = redraw || true;
    }

    ImGui::InvisibleButton("##timeline",
                           ImVec2(timeline_width, std::max(timeline_area.y, area_size.y)));

    double view_scale = calc_view_scale();
    double inv_view_scale = 1.0 / view_scale;
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_wheel = ImGui::GetIO().MouseWheel;
    bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool timeline_hovered = ImGui::IsItemHovered();
    bool mouse_move = false;

    if (mouse_pos.x != last_mouse_pos.x || mouse_pos.y != last_mouse_pos.y) {
        last_mouse_pos = mouse_pos;
        mouse_move = true;
    }

    if (middle_mouse_clicked && middle_mouse_down && timeline_hovered)
        scrolling = true;

    if (scrolling) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
        scroll_horizontal(drag_delta.x, song_length, -view_scale);
        scroll_delta_y = drag_delta.y;
        if (scroll_delta_y != 0.0f)
            redraw = true;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    if (!middle_mouse_down) {
        scrolling = false;
        scroll_delta_y = 0.0f;
    }

    bool dragging_file = false;
    BrowserFilePayload item_drop {};
    if (ImGui::BeginDragDropTarget()) {
        constexpr ImGuiDragDropFlags drag_drop_flags =
            ImGuiDragDropFlags_AcceptPeekOnly | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        if (ImGui::AcceptDragDropPayload("WB_FILEDROP", drag_drop_flags)) {
            auto drop_payload = ImGui::AcceptDragDropPayload(
                "WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (drop_payload)
                std::memcpy(&item_drop, drop_payload->Data, drop_payload->DataSize);
            dragging_file = true;
        }
        redraw = true;
        ImGui::EndDragDropTarget();
    }

    float timeline_end_x = timeline_view_pos.x + timeline_width;

    // Scroll automatically when dragging stuff
    if (edit_action != TimelineEditAction::None || dragging_file) {
        static constexpr float speed = 0.25f;
        float min_offset = !dragging_file ? timeline_view_pos.x : timeline_view_pos.x + 20.0f;
        float max_offset = !dragging_file ? timeline_end_x : timeline_end_x - 20.0f;
        if (mouse_pos.x < min_offset) {
            float distance = min_offset - mouse_pos.x;
            scroll_horizontal(distance * speed * (float)inv_view_scale, song_length, -view_scale);
        }
        if (mouse_pos.x > max_offset) {
            float distance = max_offset - mouse_pos.x;
            scroll_horizontal(distance * speed * (float)inv_view_scale, song_length, -view_scale);
        }
        redraw = true;
    }

    double sample_scale = (view_scale * beat_duration) * inv_ppq;
    double scroll_pos_x = (min_hscroll * song_length) / view_scale;

    // Map mouse position to time position
    double mouse_at_time_pos =
        ((double)(mouse_pos.x - timeline_view_pos.x) * view_scale + min_hscroll * song_length) *
        inv_ppq;
    double mouse_at_gridline =
        std::round(mouse_at_time_pos * (double)grid_scale) / (double)grid_scale;

    ImU32 grid_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.55f);
    ImColor text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    double timeline_scroll_offset_x = (double)timeline_view_pos.x - scroll_pos_x;
    float timeline_scroll_offset_x_f32 = (float)timeline_scroll_offset_x;
    float font_size = ImGui::GetFontSize();
    float track_pos_y = timeline_view_pos.y;
    float drop_file_pos_y = 0.0f;
    double clip_scale = ppq * inv_view_scale;
    ImFont* font = ImGui::GetFont();

    redraw = redraw || (mouse_move && edit_action != TimelineEditAction::None) || dragging_file;

    if (redraw) {
        clip_content_cmds.resize(0);
        priv_draw_list->_ResetForNewFrame();
        priv_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        priv_draw_list->PushClipRect(view_min, view_max);

        float grid_inc_x = (float)(ppq / view_scale / (double)grid_scale);
        float inv_grid_inc_x = 1.0f / grid_inc_x;
        float gridline_pos_x = timeline_view_pos.x - std::fmod((float)scroll_pos_x, grid_inc_x);
        int gridline_count = (uint32_t)(timeline_width * inv_grid_inc_x);
        int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
        for (int i = 0; i <= gridline_count; i++) {
            gridline_pos_x += grid_inc_x;
            priv_draw_list->AddLine(ImVec2(std::round(gridline_pos_x), offset_y),
                                    ImVec2(std::round(gridline_pos_x), offset_y + area_size.y),
                                    grid_color, (i + count_offset + 1) % 4 ? 1.0f : 2.0f);
        }

        //Log::info("{}")
        //Log::info("{} {} {}", view_max.y, view_max.y, vscroll + timeline_view_pos.y);
    }

    for (auto track : g_engine.tracks) {
        float height = track->height;

        if (track_pos_y > timeline_area.y + offset_y) {
            break;
        }

        if (track_pos_y < offset_y - height - 2.0f) {
            track_pos_y += height + 2.0f;
            continue;
        }

        bool hovering_track_rect =
            !scrolling && ImGui::IsMouseHoveringRect(ImVec2(timeline_view_pos.x, track_pos_y),
                                                     ImVec2(timeline_end_x, track_pos_y + height));
        bool hovering_current_track = timeline_hovered && hovering_track_rect;

        for (auto clip : track->clips) {
            double min_time = clip->min_time;
            double max_time = clip->max_time;

            // This clip being edited, draw this clip in the front
            if (clip == edited_clip || (min_time == max_time))
                continue;

            double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
            double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;
            float min_pos_x_in_pixel = (float)std::round(min_pos_x);
            float max_pos_x_in_pixel = (float)std::round(max_pos_x);

            // Skip out-of-screen clips.
            if (min_pos_x_in_pixel > timeline_end_x)
                break;
            if (max_pos_x_in_pixel < timeline_view_pos.x)
                continue;

            ImVec2 min_bb(min_pos_x_in_pixel, track_pos_y);
            ImVec2 max_bb(max_pos_x_in_pixel, track_pos_y + track->height);
            // ImVec4 fine_scissor_rect(min_bb.x, min_bb.y, max_bb.x, max_bb.y);
            bool hovering_left_side = false;
            bool hovering_right_side = false;

            if (hovering_current_track && edit_action == TimelineEditAction::None) {
                ImRect clip_rect(min_bb, max_bb);
                // Sizing handle hitboxes
                ImRect lhs(min_pos_x_in_pixel, track_pos_y, min_pos_x_in_pixel + 4.0f, max_bb.y);
                ImRect rhs(max_pos_x_in_pixel - 4.0f, track_pos_y, max_pos_x_in_pixel, max_bb.y);

                // Assign edit action
                if (lhs.Contains(mouse_pos)) {
                    if (left_mouse_clicked)
                        edit_action = TimelineEditAction::ClipResizeLeft;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    hovering_left_side = true;
                } else if (rhs.Contains(mouse_pos)) {
                    if (left_mouse_clicked)
                        edit_action = TimelineEditAction::ClipResizeRight;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    hovering_right_side = true;
                } else if (clip_rect.Contains(mouse_pos)) {
                    if (left_mouse_clicked)
                        edit_action = !ImGui::IsKeyDown(ImGuiKey_LeftShift)
                                          ? TimelineEditAction::ClipMove
                                          : TimelineEditAction::ClipDuplicate;
                    else if (right_mouse_clicked)
                        edit_action = TimelineEditAction::ShowClipContextMenu;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                }

                if (edit_action != TimelineEditAction::None) {
                    initial_time_pos = mouse_at_gridline;
                    edited_track = track;
                    edited_track_pos_y = track_pos_y;
                    edited_clip = clip;
                    continue;
                }
            }

            double start_sample_pos = (double)clip->audio.min_sample_pos;

            if (redraw) {
                /*draw_clip(priv_draw_list, clip_content_cmds, track, clip, timeline_view_pos.x,
                          offset_y, timeline_width_f64, sample_scale, clip_scale,
                          (double)clip->audio.start_sample_pos, min_bb, max_bb, text_color, font,
                          font_size);*/
                draw_clip2(priv_draw_list, clip_content_cmds, clip, timeline_width, offset_y,
                           timeline_view_pos.x, min_pos_x, max_pos_x, clip_scale, sample_scale,
                           start_sample_pos, track_pos_y, track->height, track->color, font,
                           text_color);
            }
        }

        // Handle file drag & drop
        if (hovering_track_rect && dragging_file) {
            // Highlight drop target
            float highlight_pos = (float)mouse_at_gridline; // Snap to grid
            priv_draw_list->AddRectFilled(
                ImVec2(timeline_scroll_offset_x_f32 + highlight_pos * (float)clip_scale,
                       track_pos_y),
                ImVec2(timeline_scroll_offset_x_f32 + (highlight_pos + 1.0f) * (float)clip_scale,
                       track_pos_y + height),
                ImGui::GetColorU32(ImGuiCol_Border));

            if (item_drop.item)
                force_redraw = true;

            // We have file dropped
            if (item_drop.item) {
                std::filesystem::path file_path =
                    item_drop.item->get_file_path(*item_drop.root_dir);

                g_engine.edit_lock();
                auto sample_asset =
                    g_engine.add_audio_clip_from_file(track, file_path, mouse_at_gridline);
                if (!sample_asset)
                    Log::info("Failed to add new clip from file");
                g_engine.edit_unlock();

                Log::info("Dropped at: {}", mouse_at_gridline);
            }
        }

        track_pos_y += track->height;

        if (redraw) {
            priv_draw_list->AddLine(
                ImVec2(timeline_view_pos.x, track_pos_y + 0.5f),
                ImVec2(timeline_view_pos.x + timeline_width, track_pos_y + 0.5f), grid_color, 2.0f);
        }

        track_pos_y += 2.0f;
    }

    // Visualize the edited clip
    if (edited_clip && redraw) {
        double min_time = edited_clip->min_time;
        double max_time = edited_clip->max_time;
        double start_sample_pos = (double)edited_clip->audio.min_sample_pos;

        switch (edit_action) {
            case TimelineEditAction::ClipMove: {
                double new_min_time =
                    std::max(min_time + mouse_at_gridline - initial_time_pos, 0.0);
                max_time = new_min_time + (max_time - min_time);
                min_time = new_min_time;

                // Readjust song length
                if (max_time * ppq > song_length) {
                    double new_song_length = std::max(max_time * ppq, song_length);
                    min_hscroll = min_hscroll * song_length / new_song_length;
                    max_hscroll = max_hscroll * song_length / new_song_length;
                    song_length = new_song_length;
                }
                break;
            }
            case TimelineEditAction::ClipResizeLeft: {
                double old_min = min_time;
                min_time = std::max(min_time + mouse_at_gridline - initial_time_pos, 0.0);
                if (min_time >= max_time)
                    min_time = max_time - (1.0 / grid_scale);

                double rel_offset = edited_clip->relative_start_time;
                if (old_min < min_time)
                    rel_offset -= old_min - min_time;
                else
                    rel_offset += min_time - old_min;
                rel_offset = std::max(rel_offset, 0.0);

                if (edited_clip->type == ClipType::Audio) {
                    SampleAsset* asset = edited_clip->audio.asset;
                    start_sample_pos = beat_to_samples(
                        rel_offset, (double)asset->sample_instance.sample_rate, beat_duration);
                }

                break;
            }
            case TimelineEditAction::ClipResizeRight:
                max_time = std::max(max_time + mouse_at_gridline - initial_time_pos, 0.0);
                if (max_time <= min_time)
                    max_time = min_time + (1.0 / grid_scale);
                break;
            case TimelineEditAction::ClipDuplicate: {
                float highlight_pos = (float)mouse_at_gridline; // Snap to grid
                float length = (float)(edited_clip->max_time - edited_clip->min_time);
                priv_draw_list->AddRectFilled(
                    ImVec2(timeline_scroll_offset_x_f32 + highlight_pos * (float)clip_scale,
                           edited_track_pos_y),
                    ImVec2(timeline_scroll_offset_x_f32 +
                               (highlight_pos + length) * (float)clip_scale,
                           edited_track_pos_y + edited_track->height),
                    ImGui::GetColorU32(ImGuiCol_Border));
                break;
            }
            default:
                break;
        }

        double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
        double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;

        if (min_pos_x < timeline_end_x && max_pos_x > timeline_view_pos.x) {
            draw_clip2(priv_draw_list, clip_content_cmds, edited_clip, timeline_width, offset_y,
                       timeline_view_pos.x, min_pos_x, max_pos_x, clip_scale, sample_scale,
                       start_sample_pos, edited_track_pos_y, edited_track->height,
                       edited_track->color, font, text_color);
        }
        /*float min_pos_x_in_pixel = (float)std::floor(min_pos_x);
        float max_pos_x_in_pixel = (float)std::floor(max_pos_x);
        ImVec2 min_bb(min_pos_x_in_pixel, edited_track_pos_y);
        ImVec2 max_bb(max_pos_x_in_pixel, edited_track_pos_y + edited_track->height);*/

        /*draw_clip(priv_draw_list, clip_content_cmds, edited_track, edited_clip,
           timeline_view_pos.x, offset_y, timeline_width_f64, sample_scale, clip_scale,
           start_sample_pos, min_bb, max_bb, text_color, font, font_size);*/

        edited_clip_min_time = min_time;
        edited_clip_max_time = max_time;
    }

    if (redraw) {
        priv_draw_list->PopClipRect();
        priv_draw_list->PopTextureID();

        ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();
        priv_draw_data.Clear();
        priv_draw_data.AddDrawList(priv_draw_list);
        priv_draw_data.DisplayPos = view_min;
        priv_draw_data.DisplaySize = timeline_area;
        priv_draw_data.FramebufferScale.x = 1.0f;
        priv_draw_data.FramebufferScale.y = 1.0f;
        priv_draw_data.OwnerViewport = owner_viewport;

        g_renderer->set_framebuffer(timeline_fb);
        g_renderer->begin_draw(timeline_fb, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        g_renderer->render_draw_data(&priv_draw_data);
        g_renderer->draw_clip_content(clip_content_cmds);
        g_renderer->finish_draw();
    }

    // Release edit action
    if (edit_action != TimelineEditAction::None) {
        g_engine.edit_lock();
        switch (edit_action) {
            case TimelineEditAction::ClipMove:
                if (!left_mouse_down) {
                    edited_track->move_clip(edited_clip, mouse_at_gridline - initial_time_pos,
                                            beat_duration);
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                break;
            case TimelineEditAction::ClipResizeLeft:
                if (!left_mouse_down) {
                    double relative_pos = mouse_at_gridline - initial_time_pos;
                    edited_track->resize_clip(edited_clip, relative_pos, 1.0 / grid_scale,
                                              beat_duration, true);
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipResizeRight:
                if (!left_mouse_down) {
                    double relative_pos = mouse_at_gridline - initial_time_pos;
                    edited_track->resize_clip(edited_clip, relative_pos, 1.0 / grid_scale,
                                              beat_duration, false);
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipDuplicate:
                if (!left_mouse_down) {
                    double clip_length = edited_clip->max_time - edited_clip->min_time;
                    edited_track->duplicate_clip(edited_clip, mouse_at_gridline,
                                                 mouse_at_gridline + clip_length, beat_duration);
                    finish_edit_action();
                    force_redraw = true;
                }
                break;
            case TimelineEditAction::ShowClipContextMenu:
                ImGui::OpenPopup("clip_context_menu");
                context_menu_track = edited_track;
                context_menu_clip = edited_clip;
                tmp_color = edited_clip->color;
                tmp_name = edited_clip->name;
                finish_edit_action();
                break;
            default:
                break;
        }
        g_engine.edit_unlock();
    }

    clip_context_menu();

    ImTextureID tex_id = g_renderer->prepare_as_imgui_texture(timeline_fb);
    win_draw_list->AddImage(tex_id, ImVec2(timeline_view_pos.x, offset_y),
                            ImVec2(timeline_view_pos.x + timeline_width, offset_y + area_size.y));

    if (timeline_hovered && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, timeline_view_pos.x, view_scale, mouse_wheel);
        zooming = true;
        force_redraw = true;
    }

    last_vscroll = vscroll;

    ImGui::PopClipRect();
}

inline void GuiTimeline::scroll_horizontal(float drag_delta, double max_length, double direction) {
    double norm_drag_delta = ((double)drag_delta / max_length) * direction;
    if (drag_delta != 0.0f) {
        double new_min_hscroll = min_hscroll + norm_drag_delta;
        double new_max_hscroll = max_hscroll + norm_drag_delta;
        if (new_min_hscroll >= 0.0f && new_max_hscroll <= 1.0f) {
            min_hscroll = new_min_hscroll;
            max_hscroll = new_max_hscroll;
        } else {
            // Clamp
            if (new_min_hscroll < 0.0) {
                min_hscroll = 0.0;
                max_hscroll = new_max_hscroll + std::abs(new_min_hscroll);
            } else if (new_max_hscroll > 1.0) {
                min_hscroll = new_min_hscroll - (new_max_hscroll - 1.0);
                max_hscroll = 1.0;
            }
        }
        redraw = true;
    }
}

inline void GuiTimeline::zoom(float mouse_pos_x, float cursor_pos_x, double view_scale,
                              float mouse_wheel) {
    double zoom_position =
        ((double)(mouse_pos_x - cursor_pos_x) / song_length * view_scale) + min_hscroll;
    if (zoom_position <= 1.0) {
        double dist_from_start = zoom_position - min_hscroll;
        double dist_to_end = max_hscroll - zoom_position;
        mouse_wheel *= 0.1f;
        min_hscroll = std::clamp(min_hscroll + dist_from_start * mouse_wheel, 0.0, max_hscroll);
        max_hscroll = std::clamp(max_hscroll - dist_to_end * mouse_wheel, min_hscroll, 1.0);
        redraw = true;
    }
}

GuiTimeline g_timeline;

} // namespace wb