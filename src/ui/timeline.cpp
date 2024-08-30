#include "timeline.h"
#include "browser.h"
#include "command.h"
#include "command_manager.h"
#include "controls.h"
#include "core/color.h"
#include "core/debug.h"
#include "core/math.h"
#include "engine/clip_edit.h"
#include "engine/engine.h"
#include "engine/track.h"
#include "forms.h"
#include "layout.h"
#include "popup_state_manager.h"
#include <fmt/format.h>

#define DEBUG_MIDI_CLIPS 0

#ifdef NDEBUG
#undef DEBUG_MIDI_CLIPS
#define DEBUG_MIDI_CLIPS 0
#endif

namespace wb {

void draw_clip(ImDrawList* layer1_draw_list, ImDrawList* layer2_draw_list,
               ImVector<ClipContentDrawCmd>& clip_content_cmds, const Clip* clip,
               float timeline_width, float offset_y, float min_draw_x, double min_x, double max_x,
               double clip_scale, double sample_scale, double start_offset, float track_pos_y,
               float track_height, const ImColor& track_color, const ImColor& text_color,
               ImFont* font) {
    constexpr ImDrawListFlags draw_list_aa_flags = ImDrawListFlags_AntiAliasedFill |
                                                   ImDrawListFlags_AntiAliasedLinesUseTex |
                                                   ImDrawListFlags_AntiAliasedLines;

    // Color accessibility adjustments...
    static constexpr float border_contrast_ratio = 1.0f / 3.5f;
    static constexpr float text_contrast_ratio = 1.0f / 1.57f;
    float bg_contrast_ratio = calc_contrast_ratio(clip->color, text_color);
    ImColor border_color = (bg_contrast_ratio > border_contrast_ratio)
                               ? ImColor(0.0f, 0.0f, 0.0f, 0.3f)
                               : ImColor(1.0f, 1.0f, 1.0f, 0.2f);
    ImColor text_color_adjusted = (bg_contrast_ratio > text_contrast_ratio)
                                      ? ImColor(0.0f, 0.0f, 0.0f, 1.0f - bg_contrast_ratio * 0.45f)
                                      : text_color;

    bool is_active = clip->is_active();
    ImColor color = is_active ? clip->color : color_adjust_alpha(clip->color, 0.75f);
    float min_x2 = (float)math::round(min_x);
    float max_x2 = (float)math::round(max_x);
    float font_size = font->FontSize;
    float clip_title_max_y = track_pos_y + font_size + 4.0f;
    ImColor bg_color = color_adjust_alpha(color, color.Value.w * 0.35f);
    ImU32 content_color =
        is_active ? color_brighten(color, 1.0f) : color_premul_alpha(color_brighten(color, 1.0f));
    ImVec2 clip_title_min_bb(min_x2, track_pos_y);
    ImVec2 clip_title_max_bb(max_x2, clip_title_max_y);
    ImVec2 clip_content_min(min_x2, clip_title_max_y);
    ImVec2 clip_content_max(max_x2, track_pos_y + track_height);
    ImDrawListFlags tmp_flags = layer1_draw_list->Flags;

    // Draw clip background and its header
    layer1_draw_list->Flags = layer1_draw_list->Flags & ~draw_list_aa_flags;
    layer1_draw_list->AddRectFilled(clip_title_min_bb, clip_title_max_bb, color);
    layer1_draw_list->AddRectFilled(clip_content_min, clip_content_max, bg_color);
    layer1_draw_list->AddRect(clip_title_min_bb, clip_title_max_bb, border_color);
    layer1_draw_list->Flags = tmp_flags;

    if (!is_active) {
        text_color_adjusted = color_adjust_alpha(text_color_adjusted, 0.75f);
    }

    // Draw clip label
    const char* str = clip->name.c_str();
    ImVec4 clip_label_rect(clip_title_min_bb.x, clip_title_min_bb.y, clip_title_max_bb.x - 6.0f,
                           clip_title_max_y);
    layer1_draw_list->AddText(
        font, font_size,
        ImVec2(std::max(clip_title_min_bb.x, min_draw_x) + 4.0f, track_pos_y + 2.0f),
        text_color_adjusted, str, str + clip->name.size(), 0.0f, &clip_label_rect);

    static constexpr double log_base4 = 1.0 / 1.3862943611198906; // 1.0 / log(4.0)
    switch (clip->type) {
        case ClipType::Audio: {
            SampleAsset* asset = clip->audio.asset;
            if (asset) {
                SamplePeaks* sample_peaks = asset->peaks.get();
                double scale_x = sample_scale * (double)asset->sample_instance.sample_rate;
                double inv_scale_x = 1.0 / scale_x;
                double mip_index = (std::log(scale_x * 0.5) * log_base4) * 0.5; // Scale -> Index
                uint32_t index =
                    std::clamp((uint32_t)mip_index, 0u, sample_peaks->mipmap_count - 1);
                double mult = std::pow(4.0, (double)index - 1.0);
                double mip_scale = std::pow(4.0, 2.0 * (mip_index - (double)index)) * 8.0 *
                                   mult; // Index -> Mip Scale
                // double mip_index = std::log(scale_x * 0.5) * log_base4; // Scale -> Index
                // double mip_scale = std::pow(4.0, (mip_index - (double)index)) * 2.0; // Index ->
                // Mip Scale

                double waveform_start = start_offset * inv_scale_x;
                double waveform_len =
                    ((double)asset->sample_instance.count - start_offset) * inv_scale_x;
                double rel_min_x = min_x - (double)min_draw_x;
                double rel_max_x = max_x - (double)min_draw_x;
                double min_pos_x = math::max(rel_min_x, 0.0);
                double max_pos_x = math::min(math::min(rel_max_x, rel_min_x + waveform_len),
                                             (double)(timeline_width + 2.0));
                double draw_count = math::max(max_pos_x - min_pos_x, 0.0);
                double start_idx = math::max(-rel_min_x, 0.0) + waveform_start;

                if (draw_count) {
                    clip_content_cmds.push_back({
                        .peaks = sample_peaks,
                        .min_bb =
                            ImVec2(math::round((float)min_pos_x), clip_content_min.y - offset_y),
                        .max_bb =
                            ImVec2(math::round((float)max_pos_x), clip_content_max.y - offset_y),
                        .color = content_color,
                        .scale_x = (float)mip_scale,
                        .mip_index = index,
                        .start_idx = (uint32_t)math::round(start_idx),
                        .draw_count = (uint32_t)draw_count + 2,
                    });
                }
            }
            break;
        }
        case ClipType::Midi: {
            constexpr float min_note_size_px = 2.5f;
            constexpr float max_note_size_px = 10.0f;
            constexpr uint32_t min_note_range = 4;
            MidiAsset* asset = clip->midi.asset;
            uint32_t min_note = asset->data.min_note;
            uint32_t max_note = asset->data.max_note;
            uint32_t note_range = (asset->data.max_note + 1) - min_note;

            if (note_range < min_note_range)
                note_range = 13;

            float view_height = clip_content_max.y - clip_content_min.y;
            float note_height = view_height / (float)note_range;
            float max_note_size = math::min(note_height, max_note_size_px);
            float min_note_size = math::max(max_note_size, min_note_size_px);
            float min_view = math::max(min_x2, min_draw_x);
            float max_view = math::min(max_x2, min_draw_x + timeline_width);
            float offset_y =
                clip_content_min.y + ((view_height * 0.5f) - (max_note_size * note_range * 0.5f));

            // Fix note overflow
            if (view_height < math::round(min_note_size * note_range)) {
                max_note_size = (view_height - 2.0f) / (float)(note_range - 1u);
            }

            if (asset) {
                uint32_t channel_count = asset->data.channel_count;
                double min_start_x = min_x - start_offset * clip_scale;
                for (uint32_t i = 0; i < channel_count; i++) {
                    const MidiNoteBuffer& buffer = asset->data.channels[i];
                    for (size_t j = 0; j < buffer.size(); j++) {
                        const MidiNote& note = buffer[j];
                        float min_pos_x =
                            (float)math::round(min_start_x + note.min_time * clip_scale);
                        float max_pos_x =
                            (float)math::round(min_start_x + note.max_time * clip_scale);
                        if (max_pos_x < min_view)
                            continue;
                        if (min_pos_x > max_view)
                            break;
                        float pos_y =
                            offset_y + (float)(max_note - note.note_number) * max_note_size;
                        min_pos_x = math::max(min_pos_x, min_view);
                        max_pos_x = math::min(max_pos_x, max_view);
                        if (min_pos_x >= max_pos_x)
                            continue;
                        ImVec2 a(min_pos_x + 0.5f, pos_y);
                        ImVec2 b(max_pos_x, pos_y + min_note_size - 0.5f);
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

    layer2_draw_list->PushClipRect(clip_content_min, clip_content_max, true);
    // layer2_draw_list->AddLine(clip_content_min, clip_content_max, border_color, 3.5f);
    // layer2_draw_list->AddLine(clip_content_min, clip_content_max, content_color, 1.5f);

    if (clip->hover_state == ClipHover::All) {
        layer2_draw_list->AddCircle(clip_content_min, 6.0f, border_color, 0, 3.5f);
        layer2_draw_list->AddCircleFilled(clip_content_min, 6.0f, content_color);
        layer2_draw_list->AddCircle(ImVec2(clip_content_max.x, clip_content_min.y), 6.0f,
                                    border_color, 0, 3.5f);
        layer2_draw_list->AddCircleFilled(ImVec2(clip_content_max.x, clip_content_min.y), 6.0f,
                                          content_color);
    }

    layer2_draw_list->PopClipRect();

    if (clip->hover_state != ClipHover::None) {
        switch (clip->hover_state) {
            case ClipHover::LeftHandle: {
                ImVec2 min_bb(min_x2, track_pos_y);
                ImVec2 max_bb(max_x2, track_pos_y + track_height);
                layer2_draw_list->AddLine(ImVec2(min_bb.x + 1.0f, min_bb.y),
                                          ImVec2(min_bb.x + 1.0f, max_bb.y),
                                          ImGui::GetColorU32(ImGuiCol_ButtonActive), 3.0f);
                break;
            }
            case ClipHover::RightHandle: {
                ImVec2 min_bb(min_x2, track_pos_y);
                ImVec2 max_bb(max_x2, track_pos_y + track_height);
                layer2_draw_list->AddLine(ImVec2(max_bb.x - 2.0f, min_bb.y),
                                          ImVec2(max_bb.x - 2.0f, max_bb.y),
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
    g_engine.add_on_bpm_change_listener(
        [this](double bpm, double beat_duration) { force_redraw = true; });
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
    float saturation = sat_pos * (0.70f - 0.55f) + 0.55f;
    Log::debug("{} {} {}", hue, sat_pos, saturation);
    track->color = ImColor::HSV(hue, saturation, 0.75f);
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
    area_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);
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
    ImGui::End();
}

// Render separator (resizer) between the track control and the track lane
void GuiTimeline::render_separator() {
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
        separator_pos = std::max(separator_pos, min_track_control_size);
    }

    float clamped_separator_pos = std::max(separator_pos, min_track_control_size);
    float separator_x = layout.main_pos.x + clamped_separator_pos + 0.5f;
    /*ImU32 separator_color = (is_separator_hovered || is_separator_active)
                                ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                                : ImGui::GetColorU32(ImGuiCol_Separator);*/
    main_draw_list->AddLine(ImVec2(separator_x, pos.y), ImVec2(separator_x, pos.y + area_size.y),
                            ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

    layout.end();

    timeline_view_pos.x = layout.main_pos.x + clamped_separator_pos + 2.0f;
    timeline_view_pos.y = layout.main_pos.y;
}

void GuiTimeline::render_track_controls() {
    constexpr ImGuiWindowFlags track_control_window_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding;

    static constexpr float track_color_width = 8.0f;
    float clamped_separator_pos = std::max(separator_pos, min_track_control_size);
    ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const auto& style = ImGui::GetStyle();

    ImGui::PushClipRect(
        screen_pos,
        ImVec2(screen_pos.x + clamped_separator_pos, screen_pos.y + area_size.y + vscroll), true);

    auto& tracks = g_engine.tracks;
    for (uint32_t i = 0; i < tracks.size(); i++) {
        Track* track = tracks[i];
        float height = track->height;
        ImVec2 tmp_item_spacing = style.ItemSpacing;
        ImVec2 track_color_min = ImGui::GetCursorScreenPos();
        ImVec2 track_color_max =
            ImVec2(track_color_min.x + track_color_width, track_color_min.y + height);

        // Draw track color
        if (ImGui::IsRectVisible(track_color_min, track_color_max)) {
            main_draw_list->AddRectFilled(track_color_min, track_color_max,
                                          ImGui::GetColorU32((ImVec4)track->color));
        }

        ImGui::Indent(track_color_width);
        ImGui::PushID(i);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(track_color_width - 2.0f, 2.0f));
        ImGui::BeginChild("##track_control",
                          ImVec2(clamped_separator_pos - track_color_width - 11.0f, height), 0,
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
                ImGui::Text("(unnamed)", nullptr, false, false);
                ImGui::EndDisabled();
            }

            float volume = track->ui_parameter_state.volume_db;
            if (controls::param_drag_db("Vol.", &volume)) {
                track->set_volume(volume);
            }

            bool mute = track->ui_parameter_state.mute;
            if (ImGui::SmallButton("M")) {
                track->set_mute(!mute);
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::SmallButton("S")) {
                g_engine.solo_track(i);
            }

            ImGui::SameLine(0.0f, 2.0f);
            if (mute) {
                ImGui::Text("Muted");
            }

            if (ImGui::IsWindowHovered() &&
                !(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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

        if (controls::hseparator_resizer(i, &track->height, 60.0f, 30.f, 500.f))
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
                default:
                    break;
            }
            ImGui::EndMenu();
        }

        ImGui::MenuItem("Duplicate");

        if (ImGui::MenuItem("Delete")) {
            g_engine.edit_lock();
            g_engine.delete_track((uint32_t)track_id);
            g_engine.edit_unlock();
            redraw = true;
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
            g_engine.delete_clip(context_menu_track, context_menu_clip);
            recalculate_song_length();
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
        Log::info("Resizing timeline framebuffer ({}x{})", (int)timeline_width, (int)area_size.y);
        timeline_fb = g_renderer->create_framebuffer((int)timeline_width, (int)area_size.y);
        old_timeline_size.x = timeline_width;
        old_timeline_size.y = area_size.y;
        redraw = redraw || true;
    }

    // The timeline is actually just a very large button that cover almost entire screen.
    ImGui::InvisibleButton(
        "##timeline", ImVec2(timeline_width, std::max(timeline_area.y, area_size.y + vscroll)));

    double view_scale = calc_view_scale();
    double inv_view_scale = 1.0 / view_scale;
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_wheel = ImGui::GetIO().MouseWheel;
    float mouse_wheel_h = ImGui::GetIO().MouseWheelH;
    bool timeline_clicked = ImGui::IsItemActivated();
    bool left_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool middle_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    bool middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool right_mouse_clicked = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool timeline_hovered = ImGui::IsItemHovered();
    bool mouse_move = false;

    if (timeline_hovered && mouse_wheel_h != 0.0f) {
        scroll_horizontal(mouse_wheel_h, song_length, -view_scale * 64.0);
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

    bool dragging_file = false;
    BrowserFilePayload item_drop {};
    if (ImGui::BeginDragDropTarget()) {
        constexpr ImGuiDragDropFlags drag_drop_flags = ImGuiDragDropFlags_AcceptPeekOnly;
        if (ImGui::AcceptDragDropPayload("WB_FILEDROP", drag_drop_flags)) {
            auto drop_payload = ImGui::AcceptDragDropPayload(
                "WB_FILEDROP", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (drop_payload)
                std::memcpy(&item_drop, drop_payload->Data, drop_payload->DataSize);
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
        float min_offset_x = !dragging_file ? view_min.x : view_min.x + drag_offset_x;
        float max_offset_x = !dragging_file ? view_max.x : view_max.x - drag_offset_x;
        float min_offset_y = !dragging_file ? view_min.y : view_min.y + drag_offset_y;
        float max_offset_y = !dragging_file ? view_max.y : view_max.y - drag_offset_y;
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

    double sample_scale = (view_scale * beat_duration) * inv_ppq;
    double scroll_pos_x = std::round((min_hscroll * song_length) / view_scale);

    // Map mouse position to time position
    double mouse_at_time_pos =
        ((double)(mouse_pos.x - timeline_view_pos.x) * view_scale + min_hscroll * song_length) *
        inv_ppq;
    double mouse_at_gridline =
        std::round(mouse_at_time_pos * (double)grid_scale) / (double)grid_scale;

    ImU32 grid_color = (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.85f);
    ImColor text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    double timeline_scroll_offset_x = (double)timeline_view_pos.x - scroll_pos_x;
    float timeline_scroll_offset_x_f32 = (float)timeline_scroll_offset_x;
    float font_size = ImGui::GetFontSize();
    float track_pos_y = timeline_view_pos.y;
    float drop_file_pos_y = 0.0f;
    double clip_scale = ppq * inv_view_scale;
    ImFont* font = ImGui::GetFont();
    bool holding_ctrl = ImGui::IsKeyDown(ImGuiKey_ModCtrl);

    if (ImGui::IsItemFocused()) {
        if (range_selected) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                delete_selected_range();
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

    if (redraw) {
        ImU32 beat_grid_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.28f);
        ImU32 bar_grid_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.5f);

        clip_content_cmds.resize(0);
        layer1_draw_list->_ResetForNewFrame();
        layer1_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer1_draw_list->PushClipRect(view_min, view_max);
        layer2_draw_list->_ResetForNewFrame();
        layer2_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer2_draw_list->PushClipRect(view_min, view_max);
        layer3_draw_list->_ResetForNewFrame();
        layer3_draw_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
        layer3_draw_list->PushClipRect(view_min, view_max);

        // Draw four bars length guidestrip
        float four_bars = (float)(16.0 * ppq / view_scale);
        uint32_t guidestrip_count = (uint32_t)(timeline_width / four_bars) + 2;
        float guidestrip_pos_x =
            timeline_view_pos.x - std::fmod((float)scroll_pos_x, four_bars * 2.0f);
        ImU32 guidestrip_color =
            (ImU32)color_adjust_alpha(ImGui::GetColorU32(ImGuiCol_Separator), 0.12f);
        for (uint32_t i = 0; i <= guidestrip_count; i++) {
            float start_pos_x = guidestrip_pos_x;
            guidestrip_pos_x += four_bars;
            if (i % 2) {
                layer1_draw_list->AddRectFilled(ImVec2(start_pos_x, offset_y),
                                                ImVec2(guidestrip_pos_x, offset_y + area_size.y),
                                                guidestrip_color);
            }
        }

        // Finally, draw the gridline
        double beat = ppq / view_scale;
        double bar = 4.0 * beat;
        double division = std::exp2(std::round(std::log2(view_scale / 5.0)));
        double grid_inc_x = beat * division;
        float inv_grid_inc_x = 1.0f / grid_inc_x;
        uint32_t lines_per_bar = std::max((uint32_t)(bar / grid_inc_x), 1u);
        uint32_t lines_per_beat = std::max((uint32_t)(beat / grid_inc_x), 1u);
        double gridline_pos_x = (double)timeline_view_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
        int gridline_count = (uint32_t)(timeline_width * inv_grid_inc_x);
        int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
        for (int i = 0; i <= gridline_count; i++) {
            gridline_pos_x += grid_inc_x;
            float gridline_pos_x_pixel = (float)math::round(gridline_pos_x);
            uint32_t grid_id = i + count_offset + 1;
            ImU32 line_color = grid_color;
            if (grid_id % lines_per_bar) {
                line_color = bar_grid_color;
            }
            if (grid_id % lines_per_beat) {
                line_color = beat_grid_color;
            }
            layer1_draw_list->AddLine(ImVec2(gridline_pos_x_pixel, offset_y),
                                      ImVec2(gridline_pos_x_pixel, offset_y + area_size.y),
                                      line_color, 1.0f);
        }
    }

    float expand_max_y = !dragging ? 0.0f : math::max(mouse_pos.y - view_max.y, 0.0f);
    bool has_deleted_clips = g_engine.has_deleted_clips.load(std::memory_order_relaxed);

    if (has_deleted_clips) {
        g_engine.delete_lock.lock();
    }

    for (uint32_t i = 0; i < g_engine.tracks.size(); i++) {
        Track* track = g_engine.tracks[i];
        float height = track->height;
        float track_view_min_y = offset_y - height - 2.0f;
        float expand_min_y = !dragging ? 0.0f : math::max(track_view_min_y - mouse_pos.y, 0.0f);

        if (track_pos_y > view_max.y + expand_max_y) {
            break;
        }

        if (track_pos_y < track_view_min_y - expand_min_y) {
            track_pos_y += height + 2.0f;
            continue;
        }

        bool hovering_track_rect =
            !scrolling && ImGui::IsMouseHoveringRect(
                              ImVec2(timeline_view_pos.x, track_pos_y),
                              ImVec2(timeline_end_x, track_pos_y + height + 1.0f), !dragging);
        bool hovering_current_track = timeline_hovered && hovering_track_rect;

        if (!any_of(edit_action, TimelineEditAction::ClipResizeLeft,
                    TimelineEditAction::ClipResizeRight, TimelineEditAction::ClipShift)) {
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

        if (hovering_current_track && selecting_range) {
            target_sel_range.last_track = i;
        }

        float next_pos_y = track_pos_y + height;

        if (redraw) {
            layer1_draw_list->AddLine(
                ImVec2(timeline_view_pos.x, next_pos_y + 0.5f),
                ImVec2(timeline_view_pos.x + timeline_width, next_pos_y + 0.5f), grid_color, 1.0f);
        }

        for (size_t j = 0; j < track->clips.size(); j++) {
            Clip* clip = track->clips[j];

            if (has_deleted_clips && clip->is_deleted()) {
                continue;
            }

            double min_time = clip->min_time;
            double max_time = clip->max_time;

            // This clip is being edited, draw this clip on the front
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
            ImVec2 max_bb(max_pos_x_in_pixel, track_pos_y + height);
            // ImVec4 fine_scissor_rect(min_bb.x, min_bb.y, max_bb.x, max_bb.y);
            bool hovering_left_side = false;
            bool hovering_right_side = false;
            bool hovering_clip = false;
            ClipHover current_hover_state {};

            if (hovering_current_track && edit_action == TimelineEditAction::None &&
                !holding_ctrl) {
                ImRect clip_rect(min_bb, max_bb);
                // Sizing handle hitboxes
                ImRect lhs(min_pos_x_in_pixel, track_pos_y, min_pos_x_in_pixel + 4.0f, max_bb.y);
                ImRect rhs(max_pos_x_in_pixel - 4.0f, track_pos_y, max_pos_x_in_pixel, max_bb.y);

                // Assign edit action
                if (lhs.Contains(mouse_pos)) {
                    if (left_mouse_clicked)
                        edit_action = TimelineEditAction::ClipResizeLeft;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    current_hover_state = ClipHover::LeftHandle;
                } else if (rhs.Contains(mouse_pos)) {
                    if (left_mouse_clicked)
                        edit_action = TimelineEditAction::ClipResizeRight;
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
                draw_clip(layer1_draw_list, layer2_draw_list, clip_content_cmds, clip,
                          timeline_width, offset_y, timeline_view_pos.x, min_pos_x, max_pos_x,
                          clip_scale, sample_scale, clip->start_offset, track_pos_y, height,
                          track->color, text_color, font);
            }
        }

        // Handle file drag & drop
        if (hovering_track_rect && dragging_file) {
            // Highlight drop target
            float highlight_pos = (float)mouse_at_gridline; // Snap to grid
            layer3_draw_list->AddRectFilled(
                ImVec2(timeline_scroll_offset_x_f32 + highlight_pos * (float)clip_scale,
                       track_pos_y),
                ImVec2(timeline_scroll_offset_x_f32 + (highlight_pos + 1.0f) * (float)clip_scale,
                       track_pos_y + height),
                ImGui::GetColorU32(ImGuiCol_Border));

            // We have file dropped
            if (item_drop.item) {
                std::filesystem::path file_path =
                    item_drop.item->get_file_path(*item_drop.root_dir);

                g_engine.edit_lock();
                auto sample_asset =
                    g_engine.add_clip_from_file(track, file_path, mouse_at_gridline);
                if (!sample_asset)
                    Log::info("Failed to add new clip from file");
                g_engine.edit_unlock();

                Log::info("Dropped at: {}", mouse_at_gridline);
                force_redraw = true;
            }
        }

        track_pos_y = next_pos_y + 2.0f;
    }

    if (has_deleted_clips) {
        g_engine.delete_lock.unlock();
    }

    // Visualize the edited clip during the action
    if (edited_clip && redraw) {
        ClipType type = edited_clip->type;
        double min_time = edited_clip->min_time;
        double max_time = edited_clip->max_time;
        double sample_offset = (double)edited_clip->audio.sample_offset;
        double relative_pos = mouse_at_gridline - initial_time_pos;
        double content_offset = edited_clip->start_offset;

        switch (edit_action) {
            case TimelineEditAction::ClipMove: {
                auto [new_min_time, new_max_time] = calc_move_clip(edited_clip, relative_pos);
                min_time = new_min_time;
                max_time = new_max_time;
                break;
            }
            case TimelineEditAction::ClipResizeLeft: {
                double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, true);
                content_offset = rel_offset;
                min_time = new_min_time;
                hovered_track_y = edited_track_pos_y;
                break;
            }
            case TimelineEditAction::ClipResizeRight: {
                double min_length = 1.0 / grid_scale;
                auto [new_min_time, new_max_time, rel_offset] =
                    calc_resize_clip(edited_clip, relative_pos, min_length, beat_duration, false);
                max_time = new_max_time;
                hovered_track_y = edited_track_pos_y;
                break;
            }
            case TimelineEditAction::ClipShift: {
                content_offset = shift_clip_content(edited_clip, relative_pos, beat_duration);
                break;
            }
            case TimelineEditAction::ClipDuplicate: {
                double highlight_pos = mouse_at_gridline; // Snap to grid
                double length = edited_clip->max_time - edited_clip->min_time;
                float duplicate_pos_y = hovered_track_y;
                hovered_track_y = edited_track_pos_y;
                layer3_draw_list->AddRectFilled(
                    ImVec2(timeline_scroll_offset_x_f32 + (float)(highlight_pos * clip_scale),
                           duplicate_pos_y),
                    ImVec2(timeline_scroll_offset_x_f32 +
                               (float)((highlight_pos + length) * clip_scale),
                           duplicate_pos_y + edited_track->height),
                    ImGui::GetColorU32(ImGuiCol_Border));
                break;
            }
            default:
                break;
        }

        double min_pos_x = timeline_scroll_offset_x + min_time * clip_scale;
        double max_pos_x = timeline_scroll_offset_x + max_time * clip_scale;

        if (min_pos_x < timeline_end_x && max_pos_x > timeline_view_pos.x) {
            draw_clip(layer1_draw_list, layer2_draw_list, clip_content_cmds, edited_clip,
                      timeline_width, offset_y, timeline_view_pos.x, min_pos_x, max_pos_x,
                      clip_scale, sample_scale, content_offset, hovered_track_y,
                      hovered_track_height, edited_track->color, text_color, font);
        }

        edited_clip_min_time = min_time;
        edited_clip_max_time = max_time;
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
                track_pos_y += track->height + 2.0f;
            }

            static const ImU32 selection_range_fill = ImColor(28, 150, 237, 64);
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

            ImVec2 a(timeline_scroll_offset_x_f32 + (float)min_time, selection_start_y);
            ImVec2 b(timeline_scroll_offset_x_f32 + (float)max_time, selection_end_y);
            layer1_draw_list->AddRectFilled(a, b, selection_range_fill);
            layer1_draw_list->AddRect(a, b, selection_range_border);
        }

        layer3_draw_list->PopClipRect();
        layer3_draw_list->PopTextureID();

        layer2_draw_list->PopClipRect();
        layer2_draw_list->PopTextureID();

        layer1_draw_list->PopClipRect();
        layer1_draw_list->PopTextureID();

        ImGuiViewport* owner_viewport = ImGui::GetWindowViewport();

        g_renderer->set_framebuffer(timeline_fb);
        g_renderer->begin_draw(timeline_fb, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

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
                        .track_id = edited_track_id.value(),
                        .target_track_id = hovered_track_id.value(),
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
                    double clip_length = edited_clip->max_time - edited_clip->min_time;
                    if (edited_track == hovered_track) {
                        edited_track->duplicate_clip(edited_clip, mouse_at_gridline,
                                                     mouse_at_gridline + clip_length,
                                                     beat_duration);
                    } else if (hovered_track != nullptr) {
                        hovered_track->duplicate_clip(edited_clip, mouse_at_gridline,
                                                      mouse_at_gridline + clip_length,
                                                      beat_duration);
                    }
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
    }

    clip_context_menu();

    if (item_drop.item) {
        recalculate_song_length();
    }

    ImTextureID tex_id = g_renderer->prepare_as_imgui_texture(timeline_fb);
    ImVec2 img_pos(timeline_view_pos.x, offset_y);
    main_draw_list->AddImage(tex_id, img_pos, img_pos + ImVec2(timeline_width, area_size.y));

    if (g_engine.is_playing()) {
        double playhead_offset = playhead * ppq * inv_view_scale;
        float playhead_pos =
            (float)std::round(timeline_view_pos.x - scroll_pos_x + playhead_offset);
        ImVec2 playhead_line_pos(playhead_pos, offset_y);
        main_draw_list->AddLine(playhead_line_pos,
                                playhead_line_pos + ImVec2(0.0f, timeline_area.y), playhead_color);
    }

    if (timeline_hovered && holding_ctrl && mouse_wheel != 0.0f) {
        zoom(mouse_pos.x, timeline_view_pos.x, view_scale, mouse_wheel);
        zooming = true;
        force_redraw = true;
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
    recalculate_song_length();
}

void GuiTimeline::recalculate_song_length() {
    double max_length = std::numeric_limits<double>::min();

    for (auto track : g_engine.tracks) {
        if (!track->clips.empty()) {
            Clip* clip = track->clips.back();
            max_length = math::max(max_length, clip->max_time * g_engine.ppq);
        } else {
            max_length = math::max(max_length, 10000.0);
        }
    }

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

void GuiTimeline::delete_selected_range() {
    double beat_duration = g_engine.beat_duration.load(std::memory_order_relaxed);
    uint32_t first_track = target_sel_range.first_track;
    uint32_t last_track = target_sel_range.last_track;
    Vector<ClipHistory> clip_history;
    g_engine.edit_lock();

    if (last_track < first_track) {
        std::swap(first_track, last_track);
    }

    for (uint32_t i = first_track; i <= last_track; i++) {
        Track* track = g_engine.tracks[i];
        auto query_result = track->query_clip_by_range(target_sel_range.min, target_sel_range.max);
        if (!query_result) {
            continue;
        }
        Log::debug("first: {} {}, last: {} {}", query_result->first, query_result->first_offset,
                   query_result->last, query_result->last_offset);
        /*if (query_result->first == query_result->last) {

        } else {
            for (uint32_t i = query_result->first; i < query_result->last; i++) {

            }
        }*/
        g_engine.trim_track_by_range(track, query_result->first, query_result->last,
                                     target_sel_range.min, target_sel_range.max, false);
    }
    g_engine.edit_unlock();
}

GuiTimeline g_timeline;

} // namespace wb