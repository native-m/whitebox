#include "timeline.h"
#include "browser.h"
#include "command.h"
#include "command_manager.h"
#include "controls.h"
#include "core/color.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "engine/audio_io.h"
#include "engine/clip_edit.h"
#include "engine/engine.h"
#include "engine/track.h"
#include "forms.h"
#include "layout.h"
#include <fmt/format.h>

#define DEBUG_MIDI_CLIPS 0

#ifdef NDEBUG
#undef DEBUG_MIDI_CLIPS
#define DEBUG_MIDI_CLIPS 0
#endif

namespace wb {

void draw_clip(ImDrawList* layer1_draw_list, ImDrawList* layer2_draw_list,
               ImVector<ClipContentDrawCmd>& clip_content_cmds, const Clip* clip, float timeline_width, float offset_y,
               float min_draw_x, double min_x, double max_x, double clip_scale, double sample_scale,
               double start_offset, float track_pos_y, float track_height, const ImColor& track_color,
               const ImColor& text_color, ImFont* font) {
    constexpr ImDrawListFlags draw_list_aa_flags =
        ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedLines;

    // Color accessibility adjustments...
    static constexpr float border_contrast_ratio = 1.0f / 3.5f;
    static constexpr float text_contrast_ratio = 1.0f / 1.57f;
    const float bg_contrast_ratio = calc_contrast_ratio(clip->color, text_color);
    const ImColor border_color =
        (bg_contrast_ratio > border_contrast_ratio) ? ImColor(0.0f, 0.0f, 0.0f, 0.3f) : ImColor(1.0f, 1.0f, 1.0f, 0.2f);
    ImColor text_color_adjusted = (bg_contrast_ratio > text_contrast_ratio)
                                      ? ImColor(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.45f)
                                      : text_color;

    const bool is_active = clip->is_active();
    const ImVec4& rect = layer1_draw_list->_ClipRectStack.back();
    const float min_pos_x = (float)math::round(min_x);
    const float max_pos_x = (float)math::round(max_x);
    const float min_pos_clamped_x = math::max(min_pos_x, rect.x - 3.0f);
    const float max_pos_clamped_x = math::min(max_pos_x, rect.z + 3.0f);
    const float font_size = font->FontSize;
    const float clip_title_max_y = track_pos_y + font_size + 4.0f;
    const ImVec2 clip_title_min_bb(min_pos_clamped_x, track_pos_y);
    const ImVec2 clip_title_max_bb(max_pos_clamped_x, clip_title_max_y);
    const ImVec2 clip_content_min(min_pos_clamped_x, clip_title_max_y);
    const ImVec2 clip_content_max(max_pos_clamped_x, track_pos_y + track_height);
    const ImColor color = is_active ? clip->color : color_adjust_alpha(clip->color, 0.75f);
    const ImU32 bg_color = color_premul_alpha(color_adjust_alpha(color, color.Value.w * 0.72f));
    const ImU32 content_color =
        is_active ? color_brighten(color, 1.45f) : color_premul_alpha(color_brighten(color, 1.45f));

    // Draw clip background and its header
    layer1_draw_list->AddRectFilled(clip_title_min_bb, clip_title_max_bb, bg_color - 0x00070707U, 2.5f,
                                    ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
    layer1_draw_list->AddRectFilled(clip_content_min, clip_content_max, bg_color, 2.5f,
                                    ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersBottomRight);
    layer1_draw_list->AddRect(clip_title_min_bb, clip_content_max, color_adjust_alpha(clip->color, 0.6f), 2.5f);

    if (!is_active) {
        text_color_adjusted = color_adjust_alpha(text_color_adjusted, 0.75f);
    }

    // Draw clip label
    if (clip->name.size() != 0) {
        const char* str = clip->name.c_str();
        const ImVec4 clip_label_rect(clip_title_min_bb.x, clip_title_min_bb.y, clip_title_max_bb.x - 6.0f,
                                     clip_title_max_y);
        layer1_draw_list->AddText(font, font_size,
                                  ImVec2(std::max(clip_title_min_bb.x, min_draw_x) + 4.0f, track_pos_y + 2.0f),
                                  0xFFFFFFFF, str, str + clip->name.size(), 0.0f, &clip_label_rect);
    }

    static constexpr double log_base4 = 1.0 / 1.3862943611198906; // 1.0 / log(4.0)
    switch (clip->type) {
        case ClipType::Audio: {
            SampleAsset* asset = clip->audio.asset;
            if (asset) [[likely]] {
                SamplePeaks* sample_peaks = asset->peaks.get();
                const double scale_x = sample_scale * (double)asset->sample_instance.sample_rate;
                const double inv_scale_x = 1.0 / scale_x;
                const double mip_index = (std::log(scale_x * 0.5) * log_base4) * 0.5; // Scale -> Index
                const int32_t index = math::clamp((int32_t)mip_index, 0, sample_peaks->mipmap_count - 1);
                const double mult = std::pow(4.0, (double)index - 1.0);
                const double mip_scale =
                    std::pow(4.0, 2.0 * (mip_index - (double)index)) * 8.0 * mult; // Index -> Mip Scale
                const double mip_div = math::round(scale_x / mip_scale);
                // double mip_index = std::log(scale_x * 0.5) * log_base4; // Scale -> Index
                // double mip_scale = std::pow(4.0, (mip_index - (double)index)) * 2.0; // Index -> Mip Scale

                const double waveform_start = start_offset * inv_scale_x;
                const double waveform_len = ((double)asset->sample_instance.count - start_offset) * inv_scale_x;
                const double rel_min_x = min_x - (double)min_draw_x;
                const double rel_max_x = max_x - (double)min_draw_x;
                const double min_pos_x = math::max(rel_min_x, 0.0);
                const double max_pos_x =
                    math::min(math::min(rel_max_x, rel_min_x + waveform_len), (double)(timeline_width + 2.0));
                const double draw_count = math::max(max_pos_x - min_pos_x, 0.0);
                const double start_idx = math::max(-rel_min_x, 0.0) + waveform_start;

                /*Log::debug("{} {} {} {} {}", sample_peaks->sample_count / (size_t)mip_div, mip_div, index,
                           math::round(start_offset / mip_div), mip_scale);*/

                if (draw_count) {
                    if (sample_peaks->channels == 2) {
                        const float height = (clip_content_max.y - clip_content_min.y) * 0.5f;
                        const float pos_y = clip_content_min.y - offset_y;
                        const float min_bb_x = (float)math::round(min_pos_x);
                        const float max_bb_x = (float)math::round(max_pos_x);
                        clip_content_cmds.push_back({
                            .peaks = sample_peaks,
                            .min_bb = ImVec2(min_bb_x, pos_y),
                            .max_bb = ImVec2(max_bb_x, pos_y + height),
                            .color = content_color,
                            .scale_x = (float)mip_scale,
                            .mip_index = index,
                            .channel = 0,
                            .start_idx = (uint32_t)math::round(start_idx),
                            .draw_count = (uint32_t)draw_count + 2,
                        });
                        clip_content_cmds.push_back({
                            .peaks = sample_peaks,
                            .min_bb = ImVec2(min_bb_x, pos_y + height),
                            .max_bb = ImVec2(max_bb_x, pos_y + height * 2.0f),
                            .color = content_color,
                            .scale_x = (float)mip_scale,
                            .mip_index = index,
                            .channel = 1,
                            .start_idx = (uint32_t)math::round(start_idx),
                            .draw_count = (uint32_t)draw_count + 2,
                        });
                    } else {
                        clip_content_cmds.push_back({
                            .peaks = sample_peaks,
                            .min_bb = ImVec2(math::round((float)min_pos_x), clip_content_min.y - offset_y),
                            .max_bb = ImVec2(math::round((float)max_pos_x), clip_content_max.y - offset_y),
                            .color = content_color,
                            .scale_x = (float)mip_scale,
                            .mip_index = index,
                            .start_idx = (uint32_t)math::round(start_idx),
                            .draw_count = (uint32_t)draw_count + 2,
                        });
                    }
                }
            }
            break;
        }
        case ClipType::Midi: {
            constexpr float min_note_size_px = 2.5f;
            constexpr float max_note_size_px = 10.0f;
            constexpr uint32_t min_note_range = 4;
            const MidiAsset* asset = clip->midi.asset;
            const uint32_t min_note = asset->data.min_note;
            const uint32_t max_note = asset->data.max_note;
            uint32_t note_range = (asset->data.max_note + 1) - min_note;

            if (note_range < min_note_range)
                note_range = 13;

            const float content_height = clip_content_max.y - clip_content_min.y;
            const float note_height = content_height / (float)note_range;
            float max_note_size = math::min(note_height, max_note_size_px);
            const float min_note_size = math::max(max_note_size, min_note_size_px);
            const float min_view = math::max(min_pos_clamped_x, min_draw_x);
            const float max_view = math::min(max_pos_clamped_x, min_draw_x + timeline_width);
            const float offset_y = clip_content_min.y + ((content_height * 0.5f) - (max_note_size * note_range * 0.5f));

            // Fix note overflow
            if (content_height < math::round(min_note_size * note_range)) {
                max_note_size = (content_height - 2.0f) / (float)(note_range - 1u);
            }

            if (asset) [[likely]] {
                uint32_t channel_count = asset->data.channel_count;
                double min_start_x = min_x - start_offset * clip_scale;
                for (uint32_t i = 0; i < channel_count; i++) {
                    const MidiNoteBuffer& buffer = asset->data.channels[i];
                    for (size_t j = 0; j < buffer.size(); j++) {
                        const MidiNote& note = buffer[j];
                        float min_pos_x = (float)math::round(min_start_x + note.min_time * clip_scale);
                        float max_pos_x = (float)math::round(min_start_x + note.max_time * clip_scale);
                        if (max_pos_x < min_view)
                            continue;
                        if (min_pos_x > max_view)
                            break;
                        const float pos_y = offset_y + (float)(max_note - note.note_number) * max_note_size;
                        min_pos_x = math::max(min_pos_x, min_view);
                        max_pos_x = math::min(max_pos_x, max_view);
                        if (min_pos_x >= max_pos_x)
                            continue;
                        const ImVec2 a(min_pos_x + 0.5f, pos_y);
                        const ImVec2 b(max_pos_x, pos_y + min_note_size - 0.5f);
#if DEBUG_MIDI_CLIPS == 1
                        char c[32] {};
                        fmt::format_to_n(c, std::size(c), "ID: {}", j);
                        layer2_draw_list->AddText(a - ImVec2(0.0f, 13.0f), 0xFFFFFFFF, c);
#endif
                        layer1_draw_list->PathLineTo(a);
                        layer1_draw_list->PathLineTo(ImVec2(b.x, a.y));
                        layer1_draw_list->PathLineTo(b);
                        layer1_draw_list->PathLineTo(ImVec2(a.x, b.y));
                        layer1_draw_list->PathFillConvex(content_color);
                    }
                }
            }
            break;
        }
        default:
            break;
    }

#if 0
    char id[8] {};
    fmt::format_to(id, "{}", clip->id);
    layer2_draw_list->AddText(clip_content_min, 0xFFFFFFFF, id);
#endif

    layer2_draw_list->PushClipRect(clip_content_min, clip_content_max, true);
    // layer2_draw_list->AddLine(clip_content_min, clip_content_max,
    // border_color, 3.5f); layer2_draw_list->AddLine(clip_content_min,
    // clip_content_max, content_color, 1.5f);

    if (clip->hover_state == ClipHover::All) {
        const ImVec2 start_fade_pos(min_pos_x, clip_title_max_y);
        const ImVec2 end_fade_pos(max_pos_x, clip_content_min.y);
        layer2_draw_list->AddCircle(start_fade_pos, 6.0f, border_color, 0, 3.5f);
        layer2_draw_list->AddCircleFilled(start_fade_pos, 6.0f, content_color);
        layer2_draw_list->AddCircle(end_fade_pos, 6.0f, border_color, 0, 3.5f);
        layer2_draw_list->AddCircleFilled(end_fade_pos, 6.0f, content_color);
    }

    layer2_draw_list->PopClipRect();

    if (clip->hover_state != ClipHover::None) {
        switch (clip->hover_state) {
            case ClipHover::LeftHandle: {
                ImVec2 min_bb(min_pos_x, track_pos_y);
                ImVec2 max_bb(max_pos_x, track_pos_y + track_height);
                layer2_draw_list->AddLine(ImVec2(min_bb.x + 1.0f, min_bb.y), ImVec2(min_bb.x + 1.0f, max_bb.y),
                                          ImGui::GetColorU32(ImGuiCol_ButtonActive), 3.0f);
                break;
            }
            case ClipHover::RightHandle: {
                ImVec2 min_bb(min_pos_x, track_pos_y);
                ImVec2 max_bb(max_pos_x, track_pos_y + track_height);
                layer2_draw_list->AddLine(ImVec2(max_bb.x - 2.0f, min_bb.y), ImVec2(max_bb.x - 2.0f, max_bb.y),
                                          ImGui::GetColorU32(ImGuiCol_ButtonActive), 3.0f);
                break;
            }
            default:
                break;
        }
    }
}

void GuiTimeline::init() {
    layer1_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
    layer2_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
    layer3_draw_list = new ImDrawList(ImGui::GetDrawListSharedData());
    g_engine.add_on_bpm_change_listener([this](double bpm, double beat_duration) { force_redraw = true; });
    g_cmd_manager.add_on_history_update_listener([this] { force_redraw = true; });
    min_track_control_size = 100.0f;
}

void GuiTimeline::shutdown() {
    delete layer1_draw_list;
    delete layer2_draw_list;
    delete layer3_draw_list;
    timeline_fb.reset();
}

Track* GuiTimeline::add_track() {
    g_engine.edit_lock();
    auto track = g_engine.add_track("New Track");
    float hue = (float)color_spin / 15.0f;
    float sat_pos = std::pow(1.0 - math::abs(hue * 2.0f - 1.0f), 2.2f);
    float saturation = sat_pos * (0.7f - 0.6f) + 0.6f;
    track->color = ImColor::HSV(hue, saturation, 0.80f);
    color_spin = (color_spin + 1) % 15;
    g_engine.edit_unlock();
    redraw = true;
    return track;
}

void GuiTimeline::reset() {
    finish_edit_action();
    color_spin = 0;
}

void GuiTimeline::render() {
    if (!open)
        return;

    playhead = g_engine.playhead_ui.load(std::memory_order_relaxed);
    inv_ppq = 1.0 / g_engine.ppq;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
    if (!controls::begin_window("Timeline", &open)) {
        ImGui::PopStyleVar();
        controls::end_window();
        return;
    }
    ImGui::PopStyleVar();

    redraw = force_redraw;
    if (force_redraw)
        force_redraw = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    render_horizontal_scrollbar();
    double new_playhead_pos = 0.0;
    if (render_time_ruler(&new_playhead_pos)) {
        g_engine.set_playhead_position(new_playhead_pos);
    }
    ImGui::PopStyleVar();

    ImVec2 content_origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(content_origin.x, content_origin.y - 1.0f),
        ImVec2(content_origin.x + ImGui::GetContentRegionAvail().x, content_origin.y - 1.0f),
        ImGui::GetColorU32(ImGuiCol_Separator));

    static constexpr ImGuiWindowFlags timeline_content_flags =
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoBackground;
    ImGui::BeginChild("##timeline_content", ImVec2(), 0, timeline_content_flags);

    main_draw_list = ImGui::GetWindowDrawList();
    content_min = ImGui::GetWindowContentRegionMin();
    content_max = ImGui::GetWindowContentRegionMax();
    area_size = content_max - content_min;
    vscroll = ImGui::GetScrollY();

    ImGuiID scrollbar_id = ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
    if (scroll_delta_y != 0.0f || ImGui::GetActiveID() == scrollbar_id) {
        ImGui::SetScrollY(vscroll - scroll_delta_y);
        scroll_delta_y = 0.0f;
        redraw = true;
    }

    if ((last_vscroll - vscroll) != 0.0f)
        redraw = true;

    render_separator();
    render_track_controls();
    render_track_lanes();

    ImGui::EndChild();
    controls::end_window();
}

// Render separator (resizer) between the track control and the track lane
void GuiTimeline::render_separator() {
    Layout layout;
    ImVec2 pos = layout.next(LayoutPosition::Fixed, ImVec2(separator_pos - 2.0f, 0.0f));

    ImGui::InvisibleButton("##timeline_separator", ImVec2(4.0f, area_size.y));
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
        separator_pos = std::max(separator_pos, min_track_control_size);
    }

    const float clamped_separator_pos = std::max(separator_pos, min_track_control_size);
    const float separator_x = layout.main_pos.x + clamped_separator_pos + 0.5f;
    main_draw_list->AddLine(ImVec2(separator_x, pos.y), ImVec2(separator_x, pos.y + area_size.y),
                            ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

    layout.end();

    timeline_view_pos.x = layout.main_pos.x + clamped_separator_pos + 2.0f;
    timeline_view_pos.y = layout.main_pos.y;
}

void GuiTimeline::render_track_controls() {
    constexpr ImGuiWindowFlags track_control_window_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysUseWindowPadding;

    static constexpr float track_color_width = 8.0f;
    const float clamped_separator_pos = std::max(separator_pos, min_track_control_size);
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const auto& style = ImGui::GetStyle();
    uint32_t max_audio_input_channels = g_audio_io->max_input_channel_count;

    ImGui::PushClipRect(screen_pos, ImVec2(screen_pos.x + clamped_separator_pos, screen_pos.y + area_size.y + vscroll),
                        true);

    auto& tracks = g_engine.tracks;
    for (uint32_t i = 0; i < tracks.size(); i++) {
        Track* track = tracks[i];
        const float height = track->height;
        const ImVec2 tmp_item_spacing = style.ItemSpacing;
        const ImVec2 track_color_min = ImGui::GetCursorScreenPos();
        const ImVec2 track_color_max = ImVec2(track_color_min.x + track_color_width, track_color_min.y + height);
        const ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);

        // Draw track color
        if (ImGui::IsRectVisible(track_color_min, track_color_max)) {
            main_draw_list->AddRectFilled(track_color_min, track_color_max, ImGui::GetColorU32((ImVec4)track->color));
        }

        ImGui::Indent(track_color_width);
        ImGui::PushID(i);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(track_color_width - 2.0f, 2.0f));
        ImGui::BeginChild("##track_control", ImVec2(clamped_separator_pos - track_color_width - 11.0f, height), 0,
                          track_control_window_flags);

        {
            bool parameter_updated = false;
            ImGuiSliderFlags slider_flags = ImGuiSliderFlags_Vertical;

            ImGui::PopStyleVar();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
            controls::collapse_button("##track_collapse", &track->shown);
            ImGui::PopStyleVar();

            ImGui::SameLine(0.0f, 5.0f);
            if (track->name.size() > 0) {
                ImGui::Text(track->name.c_str(), nullptr, false, false);
            } else {
                ImGui::BeginDisabled();
                ImGui::TextEx("(unnamed)", nullptr, 0);
                ImGui::EndDisabled();
            }

            ImVec2 free_region = ImGui::GetContentRegionAvail();
            float item_height = controls::get_item_height();
            float volume = track->ui_parameter_state.volume_db;
            bool mute = track->ui_parameter_state.mute;

            if (free_region.y < item_height * 1.5f) [[likely]] {
                if (free_region.y < (item_height - style.ItemSpacing.y)) {
                    if (free_region.y >= item_height * 0.5f) {
                        // Mute & solo only
                        if (controls::small_toggle_button("M", &mute, muted_color))
                            track->set_mute(!mute);
                        ImGui::SameLine(0.0f, 2.0f);
                        if (ImGui::SmallButton("S"))
                            g_engine.solo_track(i);

                        ImGui::SameLine(0.0f, 2.0f);
                        ImGui::BeginDisabled(g_engine.recording);
                        if (controls::small_toggle_button("R", &track->arm_record, muted_color))
                            track->arm_record = !track->arm_record;
                        ImGui::EndDisabled();
                    }
                } else [[likely]] {
                    // Compact
                    if (controls::toggle_button("M", &mute, muted_color))
                        track->set_mute(!mute);

                    ImGui::SameLine(0.0f, 2.0f);
                    if (ImGui::Button("S"))
                        g_engine.solo_track(i);

                    ImGui::SameLine(0.0f, 2.0f);
                    ImGui::BeginDisabled(g_engine.recording);
                    if (controls::toggle_button("R", &track->arm_record, muted_color))
                        track->arm_record = !track->arm_record;
                    ImGui::EndDisabled();

                    ImGui::SameLine(0.0f, 2.0f);
                    ImVec2 pos = ImGui::GetCursorPos();
                    ImGui::SetNextItemWidth(free_region.x - pos.x);
                    if (controls::param_drag_db("##Vol.", &volume))
                        track->set_volume(volume);
                }
            } else {
                // Large
                if (controls::param_drag_db("Vol.", &volume))
                    track->set_volume(volume);

                if (free_region.y >= item_height * 2.5f) {
                    float pan = track->ui_parameter_state.pan;
                    if (controls::param_drag_panning("Pan", &pan)) {
                        track->set_pan(pan);
                    }
                }

                if (free_region.y >= item_height * 3.5f) {
                    constexpr ImGuiSelectableFlags selected_flags = ImGuiSelectableFlags_Highlight;
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));

                    const char* input_name = "None";
                    switch (track->input_mode) {
                        case TrackInputMode::ExternalStereo: {
                            uint32_t index_mul = track->input_index * 2;
                            ImFormatStringToTempBuffer(&input_name, nullptr, "%d+%d", index_mul + 1, index_mul + 2);
                            break;
                        }
                        case TrackInputMode::ExternalMono: {
                            ImFormatStringToTempBuffer(&input_name, nullptr, "%d", track->input_index + 1);
                            break;
                        }
                        default:
                            break;
                    }

                    ImGui::BeginDisabled(g_engine.recording);
                    if (ImGui::BeginCombo("Input", input_name)) [[unlikely]] {
                        bool none = track->input_mode == TrackInputMode::None;
                        bool ext_stereo = track->input_mode == TrackInputMode::ExternalStereo;
                        bool ext_mono = track->input_mode == TrackInputMode::ExternalMono;

                        if (ImGui::Selectable("None", none, none ? ImGuiSelectableFlags_Highlight : 0)) {
                            track->input_mode = TrackInputMode::None;
                            track->input_index = 0;
                        }

                        ImGui::Selectable("Ext. stereo", true, ImGuiSelectableFlags_Disabled);
                        for (uint32_t i = 0; i < max_audio_input_channels; i += 2) {
                            const char* name;
                            bool selected = ext_stereo && track->input_index == i;
                            ImFormatStringToTempBuffer(&name, nullptr, "%d+%d", i + 1, i + 2);
                            if (ImGui::Selectable(name, false, selected ? ImGuiSelectableFlags_Highlight : 0)) {
                                track->input_mode = TrackInputMode::ExternalStereo;
                                track->input_index = i;
                            }
                        }

                        ImGui::Selectable("Ext. mono", true, ImGuiSelectableFlags_Disabled);
                        for (uint32_t i = 0; i < max_audio_input_channels; i++) {
                            const char* name;
                            bool selected = ext_mono && track->input_index == i;
                            ImFormatStringToTempBuffer(&name, nullptr, "%d", i + 1);
                            if (ImGui::Selectable(name, false, selected ? ImGuiSelectableFlags_Highlight : 0)) {
                                track->input_mode = TrackInputMode::ExternalMono;
                                track->input_index = i;
                            }
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::EndDisabled();

                    ImGui::PopStyleVar();
                }

                if (controls::small_toggle_button("M", &mute, muted_color))
                    track->set_mute(!mute);
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::SmallButton("S"))
                    g_engine.solo_track(i);
                ImGui::SameLine(0.0f, 2.0f);

                ImGui::BeginDisabled(g_engine.recording);
                if (controls::small_toggle_button("R", &track->arm_record, muted_color))
                    track->arm_record = !track->arm_record;
                ImGui::EndDisabled();
            }

            if (ImGui::IsWindowHovered() && !(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) [[unlikely]] {
                ImGui::OpenPopup("track_context_menu");
                context_menu_track = track;
                tmp_color = track->color;
                tmp_name = track->name;
            }

            track_context_menu(*track, i);
            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
        ImGui::SameLine();

        controls::level_meter("##timeline_vu_meter", ImVec2(10.0f, height), 2, track->level_meter,
                              track->level_meter_color, false);

        ImGui::PopID();
        ImGui::Unindent(track_color_width);

        if (controls::resizable_horizontal_separator(i, &track->height, 60.0f, 30.f, 500.f))
            redraw = true;

        ImGui::PopStyleVar();
    }

    if (ImGui::Button("Add Track")) {
        add_track();
    }

    ImGui::PopClipRect();
}

void GuiTimeline::track_context_menu(Track& track, int track_id) {
    if (ImGui::BeginPopup("track_context_menu")) {
        if (track.name.size() > 0) {
            ImGui::MenuItem(track.name.c_str(), nullptr, false, false);
        } else {
            ImGui::MenuItem("(unnamed)", nullptr, false, false);
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Rename")) {
            FormResult result = rename_form(&context_menu_track->name, &tmp_name);
            if (result == FormResult::Close) {
                ImGui::CloseCurrentPopup();
                redraw = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Change color")) {
            FormResult result = color_picker_form(&context_menu_track->color, tmp_color);
            switch (result) {
                case FormResult::ValueChanged:
                    force_redraw = true;
                    break;
                case FormResult::Close:
                    ImGui::CloseCurrentPopup();
                    force_redraw = true;
                    break;
                default:
                    break;
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Apply track color to every clip")) {
            for (auto clip : track.clips)
                clip->color = track.color;
            redraw = true;
        }

        if (ImGui::MenuItem("Delete")) {
            g_engine.delete_track((uint32_t)track_id);
            redraw = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset height")) {
            ImGui::CloseCurrentPopup();
            redraw = true;
            track.height = 56.0f;
        }
        ImGui::EndPopup();
    }
}

void GuiTimeline::clip_context_menu() {
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
                default:
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
                default:
                    break;
            }
            ImGui::EndMenu();
        }

        if (!context_menu_clip->is_active()) {
            if (ImGui::MenuItem("Activate Clip")) {
                context_menu_clip->set_active(true);
                force_redraw = true;
            }
        } else {
            if (ImGui::MenuItem("Deactivate Clip")) {
                context_menu_clip->set_active(false);
                force_redraw = true;
            }
        }

        if (ImGui::MenuItem("Delete")) {
            double beat_duration = g_engine.get_beat_duration();
            g_cmd_manager.execute("Delete Clip", ClipDeleteCmd {
                                                     .track_id = context_menu_track_id.value(),
                                                     .clip_id = context_menu_clip->id,
                                                 });
            recalculate_timeline_length();
            force_redraw = true;
        }

        if (ImGui::MenuItem("Duplicate")) {
            // TODO
            force_redraw = true;
        }

        ImGui::EndPopup();
    }
}

void GuiTimeline::render_track_lanes() {
    const double ppq = g_engine.ppq;
    const double beat_duration = g_engine.beat_duration.load(std::memory_order_relaxed);
    const float offset_y = vscroll + timeline_view_pos.y;
    ImGui::SetCursorScreenPos(ImVec2(timeline_view_pos.x, timeline_view_pos.y));

    const auto timeline_area = ImGui::GetContentRegionAvail();
    const double timeline_width_f64 = (double)timeline_area.x;
    timeline_width = timeline_area.x;

    const ImVec2 view_min(timeline_view_pos.x, offset_y);
    const ImVec2 view_max(timeline_view_pos.x + timeline_width, offset_y + area_size.y);
    ImGui::PushClipRect(view_min, view_max, true);

    if (timeline_width != old_timeline_size.x || area_size.y != old_timeline_size.y) {
        Log::info("Resizing timeline framebuffer ({}x{})", (int)timeline_width, (int)area_size.y);
        timeline_fb = g_renderer->create_framebuffer((int)timeline_width, (int)area_size.y);
        old_timeline_size.x = timeline_width;
        old_timeline_size.y = area_size.y;
        redraw = redraw || true;
    }

    // The timeline is actually just a very large button that cover almost
    // entire window.
    ImGui::InvisibleButton("##timeline", ImVec2(timeline_width, std::max(timeline_area.y, area_size.y + vscroll)));

    const double view_scale = calc_view_scale();
    const double inv_view_scale = 1.0 / view_scale;
    const ImVec2 mouse_pos = ImGui::GetMousePos();
    const float mouse_wheel = ImGui::GetIO().MouseWheel;
    const float mouse_wheel_h = ImGui::GetIO().MouseWheelH;
    const bool timeline_clicked = ImGui::IsItemActivated();
    const bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool timeline_hovered = ImGui::IsItemHovered();
    bool mouse_move = false;

    if (timeline_hovered && mouse_wheel_h != 0.0f) {
        double scroll_speed = 64.0;
        scroll_horizontal(mouse_wheel_h, song_length, -view_scale * scroll_speed);
    }

    if (mouse_pos.x != last_mouse_pos.x || mouse_pos.y != last_mouse_pos.y) {
        last_mouse_pos = mouse_pos;
        mouse_move = true;
    }

    // Assign scroll
    if (middle_mouse_clicked && middle_mouse_down && timeline_hovered)
        scrolling = true;

    // Do scroll
    if (scrolling) {
        const ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 1.0f);
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

    bool dragging_file = false;
    bool item_dropped = false;
    BrowserFilePayload* drop_payload_data {};
    if (ImGui::BeginDragDropTarget()) {
        auto payload = ImGui::GetDragDropPayload();
        if (payload->IsDataType("WB_FILEDROP")) {
            item_dropped = ImGui::AcceptDragDropPayload("WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            std::memcpy(&drop_payload_data, payload->Data, payload->DataSize);
            dragging_file = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (auto payload = ImGui::GetDragDropPayload()) {
        if (payload->IsDataType("WB_FILEDROP")) {
            redraw = true;
        }
    }

    float timeline_end_x = timeline_view_pos.x + timeline_width;
    bool dragging = false;

    if (edit_action != TimelineEditAction::None || dragging_file || selecting_range) {
        static constexpr float speed = 0.25f;
        static constexpr float drag_offset_x = 20.0f;
        static constexpr float drag_offset_y = 40.0f;
        float min_offset_x;
        float max_offset_x;
        float min_offset_y;
        float max_offset_y;

        if (!dragging_file) {
            min_offset_x = view_min.x;
            max_offset_x = view_max.x;
            min_offset_y = view_min.y;
            max_offset_y = view_max.y;
        } else {
            min_offset_x = view_min.x + drag_offset_x;
            max_offset_x = view_max.x - drag_offset_x;
            min_offset_y = view_min.y + drag_offset_y;
            max_offset_y = view_max.y - drag_offset_y;
        }

        // Scroll automatically when dragging stuff
        if (mouse_pos.x < min_offset_x) {
            float distance = min_offset_x - mouse_pos.x;
            scroll_horizontal(distance * speed, song_length, -view_scale);
        }
        if (mouse_pos.x > max_offset_x) {
            float distance = max_offset_x - mouse_pos.x;
            scroll_horizontal(distance * speed, song_length, -view_scale);
        }
        if (mouse_pos.y < min_offset_y) {
            float distance = min_offset_y - mouse_pos.y;
            scroll_delta_y = distance * speed;
        }
        if (mouse_pos.y > max_offset_y) {
            float distance = max_offset_y - mouse_pos.y;
            scroll_delta_y = distance * speed;
        }

        dragging = true;
        redraw = true;
    }

    const double sample_scale = (view_scale * beat_duration) * inv_ppq;
    const double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);

    // Map mouse position to time position
    const double mouse_at_time_pos =
        ((double)(mouse_pos.x - timeline_view_pos.x) * view_scale + min_hscroll * song_length) * inv_ppq;
    const double mouse_at_gridline = std::round(mouse_at_time_pos * (double)grid_scale) / (double)grid_scale;

    static constexpr uint32_t highlight_color = 0x9F555555;
    const ImU32 gridline_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.85f);
    const ImColor text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImU32 text_color_transparent = color_adjust_alpha(text_color, 0.7f);
    const double timeline_scroll_offset_x = (double)timeline_view_pos.x - scroll_pos_x;
    const float timeline_scroll_offset_x_f32 = (float)timeline_scroll_offset_x;
    const float font_size = ImGui::GetFontSize();
    const float drop_file_pos_y = 0.0f;
    const double clip_scale = ppq * inv_view_scale;
    const bool holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
    ImFont* font = ImGui::GetFont();

    if (ImGui::IsItemFocused()) {
        if (range_selected) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                uint32_t first_track = target_sel_range.first_track;
                uint32_t last_track = target_sel_range.last_track;
                g_cmd_manager.execute("Delete Selected Region", ClipDeleteRegionCmd {
                                                                    .first_track_id = target_sel_range.first_track,
                                                                    .last_track_id = target_sel_range.last_track,
                                                                    .min_time = target_sel_range.min,
                                                                    .max_time = target_sel_range.max,
                                                                });
                recalculate_timeline_length();
                redraw = true;
            }
        }
    }

    if (range_selected && timeline_clicked) {
        range_selected = false;
        redraw = true;
    }

    if (selecting_range) {
        target_sel_range.max = math::max(mouse_at_gridline, 0.0);
        redraw = true;
    }

    if (selecting_range && !left_mouse_down) {
        target_sel_range.max = math::max(mouse_at_gridline, 0.0);
        selecting_range = false;
        range_selected = target_sel_range.max != target_sel_range.min;
        if (target_sel_range.min > target_sel_range.max) {
            std::swap(target_sel_range.min, target_sel_range.max);
        }
    }

    redraw = redraw || (mouse_move && edit_action != TimelineEditAction::None) || dragging_file;

    if (g_engine.recording)
        redraw = true;

    if (redraw) {
        static constexpr float guidestrip_alpha = 0.12f;
        static constexpr float beat_line_alpha = 0.28f;
        static constexpr float bar_line_alpha = 0.5f;
        const ImU32 guidestrip_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), guidestrip_alpha);
        const ImU32 beat_line_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), beat_line_alpha);
        const ImU32 bar_line_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), bar_line_alpha);

        clip_content_cmds.resize(0);
        layer1_draw_list->_ResetForNewFrame();
        layer2_draw_list->_ResetForNewFrame();
        layer3_draw_list->_ResetForNewFrame();
        layer1_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer2_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer3_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer1_draw_list->PushClipRect(view_min, view_max);
        layer2_draw_list->PushClipRect(view_min, view_max);
        layer3_draw_list->PushClipRect(view_min, view_max);

        // Draw four bars length guidestrip
        const float four_bars = (float)(16.0 * ppq / view_scale);
        const uint32_t guidestrip_count = (uint32_t)(timeline_width / four_bars) + 2;
        float guidestrip_pos_x = timeline_view_pos.x - std::fmod((float)scroll_pos_x, four_bars * 2.0f);
        for (uint32_t i = 0; i <= guidestrip_count; i++) {
            float start_pos_x = guidestrip_pos_x;
            guidestrip_pos_x += four_bars;
            if (i % 2) {
                layer1_draw_list->AddRectFilled(ImVec2(start_pos_x, offset_y),
                                                ImVec2(guidestrip_pos_x, offset_y + area_size.y), guidestrip_color);
            }
        }

        // Finally, draw the gridline
        static constexpr double gridline_division_factor = 5.0;
        const double beat = ppq / view_scale;
        const double bar = 4.0 * beat;
        const double division = std::exp2(std::round(std::log2(view_scale / gridline_division_factor)));
        const double grid_inc_x = beat * division;
        const double inv_grid_inc_x = 1.0 / grid_inc_x;
        const uint32_t lines_per_bar = std::max((uint32_t)(bar / grid_inc_x), 1u);
        const uint32_t lines_per_beat = std::max((uint32_t)(beat / grid_inc_x), 1u);
        const int gridline_count = (uint32_t)((double)timeline_width * inv_grid_inc_x);
        const int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
        double line_pos_x = (double)timeline_view_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
        for (int i = 0; i <= gridline_count; i++) {
            line_pos_x += grid_inc_x;
            float line_pixel_pos_x = (float)math::round(line_pos_x);
            uint32_t grid_id = i + count_offset + 1;
            ImU32 line_color = gridline_color;
            if (grid_id % lines_per_bar) {
                line_color = bar_line_color;
            }
            if (grid_id % lines_per_beat) {
                line_color = beat_line_color;
            }
            layer1_draw_list->AddLine(ImVec2(line_pixel_pos_x, offset_y),
                                      ImVec2(line_pixel_pos_x, offset_y + area_size.y), line_color, 1.0f);
        }
    }

    const float expand_max_y = !dragging ? 0.0f : math::max(mouse_pos.y - view_max.y, 0.0f);
    const bool has_deleted_clips = g_engine.has_deleted_clips.load(std::memory_order_relaxed);

    if (has_deleted_clips) {
        g_engine.delete_lock.lock();
    }

    float track_pos_y = timeline_view_pos.y;
    static constexpr float track_separator_height = 2.0f;
    for (uint32_t i = 0; i < g_engine.tracks.size(); i++) {
        Track* track = g_engine.tracks[i];
        const float height = track->height;
        const float track_view_min_y = offset_y - height - track_separator_height;
        const float expand_min_y = !dragging ? 0.0f : math::max(track_view_min_y - mouse_pos.y, 0.0f);

        if (track_pos_y > view_max.y + expand_max_y)
            break;

        if (track_pos_y < track_view_min_y - expand_min_y) {
            track_pos_y += height + track_separator_height;
            continue;
        }

        bool hovering_track_rect =
            !scrolling && ImGui::IsMouseHoveringRect(ImVec2(timeline_view_pos.x, track_pos_y),
                                                     ImVec2(timeline_end_x, track_pos_y + height + 1.0f), !dragging);
        bool hovering_current_track = timeline_hovered && hovering_track_rect;

        if (!any_of(edit_action, TimelineEditAction::ClipResizeLeft, TimelineEditAction::ClipResizeRight,
                    TimelineEditAction::ClipShift)) {
            if (left_mouse_down && hovering_track_rect) {
                hovered_track = track;
                hovered_track_id = i;
                hovered_track_y = track_pos_y;
                hovered_track_height = height;
            }
        }

        // Register start position of selection
        if (hovering_current_track && holding_ctrl && left_mouse_clicked) {
            target_sel_range.first_track = i;
            target_sel_range.min = mouse_at_gridline;
            selecting_range = true;
        }

        if (hovering_current_track && selecting_range)
            target_sel_range.last_track = i;

        const float next_pos_y = track_pos_y + height;

        if (redraw) {
            layer1_draw_list->AddLine(ImVec2(timeline_view_pos.x, next_pos_y + 0.5f),
                                      ImVec2(timeline_view_pos.x + timeline_width, next_pos_y + 0.5f), gridline_color,
                                      1.0f);
        }

        for (size_t j = 0; j < track->clips.size(); j++) {
            Clip* clip = track->clips[j];

            if (has_deleted_clips && clip->is_deleted())
                continue;

            const double min_time = clip->min_time;
            const double max_time = clip->max_time;

            // This clip is being edited, draw this clip on the front
            if (clip == edited_clip || (min_time == max_time))
                continue;

            const double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
            const double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;
            const float min_pos_x_in_pixel = (float)math::round(min_pos_x);
            const float max_pos_x_in_pixel = (float)math::round(max_pos_x);

            // Skip out-of-screen clips.
            if (min_pos_x_in_pixel > timeline_end_x)
                break;
            if (max_pos_x_in_pixel < timeline_view_pos.x)
                continue;

            const ImVec2 min_bb(min_pos_x_in_pixel, track_pos_y);
            const ImVec2 max_bb(max_pos_x_in_pixel, track_pos_y + height);
            // ImVec4 fine_scissor_rect(min_bb.x, min_bb.y, max_bb.x, max_bb.y);
            bool hovering_left_side = false;
            bool hovering_right_side = false;
            bool hovering_clip = false;
            ClipHover current_hover_state {};

            if (hovering_current_track && edit_action == TimelineEditAction::None && !holding_ctrl) {
                static constexpr float handle_offset = 4.0f;
                ImRect clip_rect(min_bb, max_bb);
                // Hitboxes for sizing handle
                ImRect left_handle(min_pos_x_in_pixel, track_pos_y, min_pos_x_in_pixel + handle_offset, max_bb.y);
                ImRect right_handle(max_pos_x_in_pixel - handle_offset, track_pos_y, max_pos_x_in_pixel, max_bb.y);

                // Assign edit action
                if (left_handle.Contains(mouse_pos)) {
                    if (left_mouse_clicked) {
                        if (!ImGui::IsKeyDown(ImGuiKey_ModAlt))
                            edit_action = TimelineEditAction::ClipResizeLeft;
                        else
                            edit_action = TimelineEditAction::ClipShiftLeft;
                    }
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    current_hover_state = ClipHover::LeftHandle;
                } else if (right_handle.Contains(mouse_pos)) {
                    if (left_mouse_clicked) {
                        if (!ImGui::IsKeyDown(ImGuiKey_ModAlt))
                            edit_action = TimelineEditAction::ClipResizeRight;
                        else
                            edit_action = TimelineEditAction::ClipShiftRight;
                    }
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    current_hover_state = ClipHover::RightHandle;
                } else if (clip_rect.Contains(mouse_pos)) {
                    if (left_mouse_clicked) {
                        if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                            edit_action = TimelineEditAction::ClipDuplicate;
                        } else if (ImGui::IsKeyDown(ImGuiKey_ModAlt)) {
                            edit_action = TimelineEditAction::ClipShift;
                        } else {
                            edit_action = TimelineEditAction::ClipMove;
                        }
                    } else if (right_mouse_clicked) {
                        edit_action = TimelineEditAction::ShowClipContextMenu;
                    }
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    current_hover_state = ClipHover::All;
                }

                if (edit_action != TimelineEditAction::None) {
                    initial_time_pos = mouse_at_gridline;
                    edited_track = track;
                    edited_track_id = i;
                    edited_track_pos_y = track_pos_y;
                    edited_clip = clip;
                    continue;
                }
            }

            if (clip->hover_state != current_hover_state) {
                clip->hover_state = current_hover_state;
                force_redraw = true;
            }

            if (redraw) {
                draw_clip(layer1_draw_list, layer2_draw_list, clip_content_cmds, clip, timeline_width, offset_y,
                          timeline_view_pos.x, min_pos_x, max_pos_x, clip_scale, sample_scale, clip->start_offset,
                          track_pos_y, height, track->color, text_color, font);
            }
        }

        // Handle file drag & drop
        if (hovering_track_rect && dragging_file) {
            // Highlight drop target
            double highlight_pos = mouse_at_gridline; // Snap to grid
            double length =
                drop_payload_data->type == BrowserItem::Sample
                    ? samples_to_beat(drop_payload_data->content_length, drop_payload_data->sample_rate, beat_duration)
                    : 1.0;
            double min_pos = highlight_pos * clip_scale;
            double max_pos = (highlight_pos + length) * clip_scale;

            layer3_draw_list->AddRectFilled(ImVec2(timeline_scroll_offset_x_f32 + (float)min_pos, track_pos_y),
                                            ImVec2(timeline_scroll_offset_x_f32 + (float)max_pos, track_pos_y + height),
                                            highlight_color);

            // We have file dropped
            if (item_dropped) {
                g_cmd_manager.execute("Add clip from file", ClipAddFromFileCmd {
                                                                .track_id = i,
                                                                .cursor_pos = mouse_at_gridline,
                                                                .file = std::move(drop_payload_data->path),
                                                            });
                Log::info("Dropped at: {}", mouse_at_gridline);
                force_redraw = true;
            }
        }

        if (track->recording) {
            const double min_pos_x = math::round(timeline_scroll_offset_x + track->record_min_time * clip_scale);
            const double max_pos_x = math::round(timeline_scroll_offset_x + track->record_max_time * clip_scale);
            const float min_clamped_pos_x = (float)math::max(min_pos_x, (double)timeline_view_pos.x);
            const float max_clamped_pos_x = (float)math::min(max_pos_x, (double)timeline_end_x);
            layer2_draw_list->AddRectFilled(ImVec2(min_clamped_pos_x, track_pos_y),
                                            ImVec2(max_clamped_pos_x, next_pos_y), highlight_color);
            layer2_draw_list->AddText(ImVec2(min_clamped_pos_x + 4.0f, track_pos_y + 2.0f), text_color_transparent,
                                      "Recording...");
        }

        track_pos_y = next_pos_y + track_separator_height;
    }

    // Visualize the edited clip during the action
    if (edited_clip && redraw) {
        ClipType type = edited_clip->type;
        double min_time = edited_clip->min_time;
        double max_time = edited_clip->max_time;
        double content_offset = edited_clip->start_offset;
        const double relative_pos = mouse_at_gridline - initial_time_pos;

        switch (edit_action) {
            case TimelineEditAction::ClipMove: {
                auto [new_min_time, new_max_time] = calc_move_clip(edited_clip, relative_pos);
                min_time = new_min_time;
                max_time = new_max_time;
                break;
            }
            case TimelineEditAction::ClipResizeLeft: {
                const double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, true);
                content_offset = rel_offset;
                min_time = new_min_time;
                hovered_track_y = edited_track_pos_y;
                break;
            }
            case TimelineEditAction::ClipResizeRight: {
                const double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, false);
                max_time = new_max_time;
                hovered_track_y = edited_track_pos_y;
                break;
            }
            case TimelineEditAction::ClipShiftLeft: {
                const double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, true, true);
                content_offset = rel_offset;
                min_time = new_min_time;
                hovered_track_y = edited_track_pos_y;
                break;
            }
            case TimelineEditAction::ClipShiftRight: {
                const double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, false, true);
                content_offset = rel_offset;
                max_time = new_max_time;
                hovered_track_y = edited_track_pos_y;
                Log::debug("{} {} {}", new_min_time, new_max_time, rel_offset);
                break;
            }
            case TimelineEditAction::ClipShift: {
                content_offset = shift_clip_content(edited_clip, relative_pos, beat_duration);
                break;
            }
            case TimelineEditAction::ClipDuplicate: {
                static constexpr uint32_t highlight_color = 0x9F555555;
                const double highlight_pos = math::max(relative_pos + edited_clip->min_time, 0.0); // Snap to grid
                const double length = edited_clip->max_time - edited_clip->min_time;
                const float duplicate_pos_y = hovered_track_y;
                hovered_track_y = edited_track_pos_y;
                layer3_draw_list->AddRectFilled(
                    ImVec2(timeline_scroll_offset_x_f32 + (float)(highlight_pos * clip_scale), duplicate_pos_y),
                    ImVec2(timeline_scroll_offset_x_f32 + (float)((highlight_pos + length) * clip_scale),
                           duplicate_pos_y + edited_track->height),
                    highlight_color);
                break;
            }
            default:
                break;
        }

        const double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
        const double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;

        if (min_pos_x < timeline_end_x && max_pos_x > timeline_view_pos.x) {
            draw_clip(layer1_draw_list, layer2_draw_list, clip_content_cmds, edited_clip, timeline_width, offset_y,
                      timeline_view_pos.x, min_pos_x, max_pos_x, clip_scale, sample_scale, content_offset,
                      hovered_track_y, hovered_track_height, edited_track->color, text_color, font);
        }

        edited_clip_min_time = min_time;
        edited_clip_max_time = max_time;
    }

    if (has_deleted_clips) {
        g_engine.delete_lock.unlock();
    }

    if (redraw) {
        if (selecting_range || range_selected) {
            float track_pos_y = timeline_view_pos.y;
            float selection_start_y = 0.0f;
            float selection_end_y = 0.0f;
            float selection_start_height = 0.0f;
            float selection_end_height = 0.0f;
            uint32_t first_track = target_sel_range.first_track;
            uint32_t last_track = target_sel_range.last_track;

            if (last_track < first_track) {
                std::swap(first_track, last_track);
            }

            for (uint32_t i = 0; i <= last_track; i++) {
                Track* track = g_engine.tracks[i];
                if (selecting_range || range_selected) {
                    if (first_track == i) {
                        selection_start_y = track_pos_y;
                        selection_start_height = track->height;
                    }
                    if (last_track == i) {
                        selection_end_y = track_pos_y;
                        selection_end_height = track->height;
                    }
                }
                track_pos_y += track->height + track_separator_height;
            }

            static const ImU32 selection_range_fill = ImColor(28, 150, 237, 78);
            static const ImU32 selection_range_border = ImColor(28, 150, 237, 127);
            double min_time = math::round(target_sel_range.min * clip_scale);
            double max_time = math::round(target_sel_range.max * clip_scale);

            if (max_time < min_time) {
                std::swap(min_time, max_time);
            }

            if (selection_end_y < selection_start_y) {
                selection_start_y += selection_start_height;
                std::swap(selection_start_y, selection_end_y);
            } else {
                selection_end_y += selection_end_height;
            }

            const ImVec2 a(timeline_scroll_offset_x_f32 + (float)min_time, selection_start_y);
            const ImVec2 b(timeline_scroll_offset_x_f32 + (float)max_time, selection_end_y);
            layer2_draw_list->AddRectFilled(a, b, selection_range_fill);
            // layer2_draw_list->AddRect(a, b, selection_range_border);
        }

        layer3_draw_list->PopClipRect();
        layer3_draw_list->PopTextureID();
        layer2_draw_list->PopClipRect();
        layer2_draw_list->PopTextureID();
        layer1_draw_list->PopClipRect();
        layer1_draw_list->PopTextureID();

        ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();

        // g_renderer->set_framebuffer(timeline_fb);
        g_renderer->begin_draw(timeline_fb.get(), ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

        layer_draw_data.Clear();
        layer_draw_data.DisplayPos = view_min;
        layer_draw_data.DisplaySize = timeline_area;
        layer_draw_data.FramebufferScale.x = 1.0f;
        layer_draw_data.FramebufferScale.y = 1.0f;
        layer_draw_data.OwnerViewport = owner_viewport;
        layer_draw_data.AddDrawList(layer1_draw_list);
        g_renderer->render_imgui_draw_data(&layer_draw_data);
        g_renderer->draw_waveforms(clip_content_cmds);

        layer_draw_data.Clear();
        layer_draw_data.DisplayPos = view_min;
        layer_draw_data.DisplaySize = timeline_area;
        layer_draw_data.FramebufferScale.x = 1.0f;
        layer_draw_data.FramebufferScale.y = 1.0f;
        layer_draw_data.OwnerViewport = owner_viewport;
        layer_draw_data.AddDrawList(layer2_draw_list);
        layer_draw_data.AddDrawList(layer3_draw_list);
        g_renderer->render_imgui_draw_data(&layer_draw_data);

        g_renderer->finish_draw();
    }

    // Release edit action
    if (edit_action != TimelineEditAction::None) {
        double relative_pos = mouse_at_gridline - initial_time_pos;
        switch (edit_action) {
            case TimelineEditAction::ClipMove:
                if (!left_mouse_down) {
                    ClipMoveCmd cmd = {
                        .src_track_id = edited_track_id.value(),
                        .dst_track_id = hovered_track_id.value(),
                        .clip_id = edited_clip->id,
                        .relative_pos = relative_pos,
                    };
                    g_cmd_manager.execute("Move Clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                break;
            case TimelineEditAction::ClipResizeLeft:
                if (!left_mouse_down) {
                    ClipResizeCmd cmd = {
                        .track_id = edited_track_id.value(),
                        .clip_id = edited_clip->id,
                        .left_side = true,
                        .relative_pos = relative_pos,
                        .min_length = 1.0 / grid_scale,
                        .last_beat_duration = beat_duration,
                    };
                    g_cmd_manager.execute("Resize clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipResizeRight:
                if (!left_mouse_down) {
                    ClipResizeCmd cmd = {
                        .track_id = edited_track_id.value(),
                        .clip_id = edited_clip->id,
                        .left_side = false,
                        .relative_pos = relative_pos,
                        .min_length = 1.0 / grid_scale,
                        .last_beat_duration = beat_duration,
                    };
                    g_cmd_manager.execute("Resize clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipShiftLeft:
                if (!left_mouse_down) {
                    ClipResizeCmd cmd = {
                        .track_id = edited_track_id.value(),
                        .clip_id = edited_clip->id,
                        .left_side = true,
                        .shift = true,
                        .relative_pos = relative_pos,
                        .min_length = 1.0 / grid_scale,
                        .last_beat_duration = beat_duration,
                    };
                    g_cmd_manager.execute("Resize and shift clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipShiftRight:
                if (!left_mouse_down) {
                    ClipResizeCmd cmd = {
                        .track_id = edited_track_id.value(),
                        .clip_id = edited_clip->id,
                        .left_side = false,
                        .shift = true,
                        .relative_pos = relative_pos,
                        .min_length = 1.0 / grid_scale,
                        .last_beat_duration = beat_duration,
                    };
                    g_cmd_manager.execute("Resize and shift clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case TimelineEditAction::ClipShift:
                if (!left_mouse_down) {
                    ClipShiftCmd cmd = {
                        .track_id = edited_track_id.value(),
                        .clip_id = edited_clip->id,
                        .relative_pos = relative_pos,
                        .last_beat_duration = beat_duration,
                    };
                    g_cmd_manager.execute("Shift clip", std::move(cmd));
                    finish_edit_action();
                    force_redraw = true;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                break;
            case TimelineEditAction::ClipDuplicate:
                if (!left_mouse_down) {
                    g_cmd_manager.execute("Duplicate Clip", ClipDuplicateCmd {
                                                                .src_track_id = edited_track_id.value(),
                                                                .dst_track_id = hovered_track_id.value(),
                                                                .clip_id = edited_clip->id,
                                                                .relative_pos = relative_pos,
                                                            });
                    finish_edit_action();
                    force_redraw = true;
                }
                break;
            case TimelineEditAction::ShowClipContextMenu:
                ImGui::OpenPopup("clip_context_menu");
                context_menu_track_id = edited_track_id;
                context_menu_track = edited_track;
                context_menu_clip = edited_clip;
                tmp_color = edited_clip->color;
                tmp_name = edited_clip->name;
                finish_edit_action();
                break;
            default:
                break;
        }
    }

    clip_context_menu();

    if (item_dropped) {
        recalculate_timeline_length();
    }

    ImTextureID tex_id = g_renderer->prepare_as_imgui_texture(timeline_fb);
    const ImVec2 img_pos(timeline_view_pos.x, offset_y);
    main_draw_list->AddImage(tex_id, img_pos, img_pos + ImVec2(timeline_width, area_size.y));

    if (g_engine.is_playing()) {
        const double playhead_offset = playhead * ppq * inv_view_scale;
        const float playhead_pos = (float)std::round(timeline_view_pos.x - scroll_pos_x + playhead_offset);
        const ImVec2 playhead_line_pos(playhead_pos, offset_y);
        main_draw_list->AddLine(playhead_line_pos, playhead_line_pos + ImVec2(0.0f, timeline_area.y), playhead_color);
    }

    if (timeline_hovered && holding_ctrl && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, timeline_view_pos.x, view_scale, mouse_wheel);
        force_redraw = true;
        zooming = true;
    }

    last_vscroll = vscroll;
    ImGui::PopClipRect();
}

void GuiTimeline::finish_edit_action() {
    hovered_track = nullptr;
    hovered_track_y = 0.0f;
    hovered_track_height = 60.0f;
    edited_clip = nullptr;
    edited_track = nullptr;
    edited_track_pos_y = 0.0f;
    edited_clip_min_time = 0.0;
    edited_clip_max_time = 0.0;
    edit_action = TimelineEditAction::None;
    initial_time_pos = 0.0;
    recalculate_timeline_length();
}

void GuiTimeline::recalculate_timeline_length() {
    double max_length = g_engine.get_song_length();
    if (max_length > 10000.0) {
        max_length += g_engine.ppq * 14;
        min_hscroll = min_hscroll * song_length / max_length;
        max_hscroll = max_hscroll * song_length / max_length;
        song_length = max_length;
    } else {
        min_hscroll = min_hscroll * song_length / 10000.0;
        max_hscroll = max_hscroll * song_length / 10000.0;
        song_length = 10000.0;
    }
}

GuiTimeline g_timeline;

} // namespace wb