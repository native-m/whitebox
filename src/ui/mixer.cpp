#include "mixer.h"

#include "context_menu.h"
#include "controls.h"
#include "core/debug.h"
#include "engine/engine2.h"
#include "engine/track.h"
#include "style.h"
#include "timeline2.h"
#include "window.h"

namespace wb {

static void render_mixer_strip_fx(Track* track, const ImVec2& size) {
  static constexpr int max_fx = 5;
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImRect bb(cursor_pos, cursor_pos + size);
  ImGuiID id = ImGui::GetID("##strip_fx");

  ImGui::ItemSize(bb);
  if (!ImGui::ItemAdd(bb, id))
    return;

  float font_size = GImGui->FontSize;
  float item_height = font_size + GImGui->Style.FramePadding.y * 2.0f + 1.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 separator_color = Color(ImGui::GetStyleColorVec4(ImGuiCol_Separator)).change_alpha(0.7f).premult_alpha().to_uint32();
  dl->PushClipRect(bb.Min, bb.Max);

  float y = cursor_pos.y;
  for (int i = 0; i < max_fx; i++) {
    y += item_height;
    im_draw_hline(dl, y, bb.Min.x, bb.Max.x, separator_color);
  }

  dl->PopClipRect();
}

static void strip_input_combo(const char* str, Track* track, uint32_t slot) {
  constexpr ImGuiSelectableFlags selected_flags = ImGuiSelectableFlags_Highlight;
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));

  IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(8.0f, -1.0f) })) {
    const char* input_name = "None";
    switch (track->input.type) {
      case TrackInputType::ExternalStereo: {
        uint32_t index_mul = track->input.index * 2;
        ImFormatStringToTempBuffer(&input_name, nullptr, "%d+%d", index_mul + 1, index_mul + 2);
        break;
      }
      case TrackInputType::ExternalMono: {
        ImFormatStringToTempBuffer(&input_name, nullptr, "%d", track->input.index + 1);
        break;
      }
      default: break;
    }

    if (ImGui::BeginCombo(str, input_name)) {
      track_input_context_menu(track, slot);
      ImGui::EndCombo();
    }
  }
}

void MixerWindow::render() {
  ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);

  ImVec2 window_padding = GImGui->Style.WindowPadding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  if (!controls::begin_window(
          "Mixer", &g_mixer_window_open, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::PopStyleVar();
    controls::end_window();
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      ImGui::MenuItem("Open mixer track state...");
      ImGui::MenuItem("Save mixer track state...");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::BeginMenu("Level meter")) {
        controls::level_meter_options();
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("+ Add Track")) {
      timeline_add_track();
    }
    ImGui::EndMenuBar();
  }

  bool is_recording = Engine2::is_recording();
  ImVec2 size = ImGui::GetContentRegionAvail();
  const NonLinearRange db_range(-72.0f, 6.0f, -2.4f);
  const LinearRange pan_range{ -1.0f, 1.0f };
  const ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);
  controls::KnobProperties pan_knob = {
    .body_color = 0xFF555555,
    .arc_color = 0xFFED961C,
    .arc_bg_color = 0xFF353535,
    .pointer_color = 0xFFAAAAAA,
    .body_size = 0.75f,
    .pointer_thickness = 2.0f,
    .pointer_min_len = 0.3f,
    .pointer_max_len = 0.9f,
    .min_angle = std::numbers::pi_v<float> / 6.0f,
    .max_angle = std::numbers::pi_v<float> * 11.0f / 6.0f,
    .bipolar = true,
  };

  controls::SliderProperties mixer_slider = {
    .grab_color = 0xFF444444,
    .grab_shape = controls::SliderGrabShape::Rectangle,
    .grab_size = { 16.0f, 28.0f },
    .grab_roundness = 3.0f,
    .extra_padding = { 0.0f, 4.0f },
    .frame_width = 4.0f,
    .with_default_value_tick = true,
  };

  // Log::info("{}", size.y);

  static constexpr float strip_width = 100.0f;
  for (int32_t id = 0; auto track : Engine2::tracks) {
    float volume = track->ui_parameter_state.volume_db;
    float pan = track->ui_parameter_state.pan;
    bool mute = track->ui_parameter_state.mute;
    ImU32 color = track->color.to_uint32();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 cursor_pos = ImGui::GetCursorPos();

    ImGui::PushID(id);

    ImGui::BeginGroup();
    if (controls::empty_region(ImVec2(strip_width, avail.y))) {
      IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(0.0f, 0.0f) })) {
        controls::strip_label(track->name.c_str(), strip_width, 1, 5, track->color);

        controls::hseparator(strip_width, 2.0f);
        if (controls::collapse_header_button("Devices", strip_width, show_devices)) {
          show_devices = !show_devices;
        }

        if (show_devices) {
          render_mixer_strip_fx(track, ImVec2(strip_width, 80.0f));
        }

        controls::hseparator(strip_width, 2.0f);
        if (controls::collapse_header_button("Sends", strip_width, show_sends)) {
          show_sends = !show_sends;
        }

        IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(0.0f, 4.0f) })) {
          controls::hseparator(strip_width, 2.0f);

          cursor_pos = ImGui::GetCursorScreenPos();
          ImGui::SetCursorScreenPos(cursor_pos + ImVec2(4.0f, 0.0f));
          ImGui::BeginGroup();
          {
            ImGui::PushItemWidth(strip_width - 8.0f);
            ImGui::Text("Input: ");
            ImGui::BeginDisabled(is_recording);
            strip_input_combo("##strip_in", track, id);
            ImGui::EndDisabled();
            ImGui::PopItemWidth();
          }
          ImGui::EndGroup();
        }
      }

      IMGUI_STYLE_BLOCK(({ ImStyleItemSpacing(8.0f, 6.0f) })) {
        controls::hseparator(strip_width, 2.0f);

        const float mix_control_width = 48.0f;
        const float mix_control_padding = (strip_width - mix_control_width) * 0.5;
        cursor_pos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(cursor_pos + ImVec2(mix_control_padding, 0.0f));
        ImGui::BeginGroup();
        {
          pan_knob.arc_color = color;
          if (controls::knob(pan_knob, "##pan_knob", ImVec2(mix_control_width, 35.0f), &pan, pan_range))
            track->set_pan(pan);

          IMGUI_STYLE_BLOCK(({ ImStyleFramePadding(2.0f, 0.0f) })) {
            const float ms_btn_width = mix_control_width * 0.5f - 1.0f;
            if (controls::toggle_button("M", &mute, muted_color, ImVec2(ms_btn_width, 0.0f)))
              track->set_mute(mute);

            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button("S", ImVec2(ms_btn_width, 0.0f)))
              Engine2::solo_track(id);
          }

          const ImVec2 region_avail = ImGui::GetContentRegionAvail();
          mixer_slider.grab_size.y = (region_avail.y < 200.0f) ? 22.0f : 28.0f;
          mixer_slider.pointer_color = color;
          // mixer_slider.grab_shade_color = track->color.brighten(0.25f).change_alpha(0.7f).premult_alpha().to_uint32();
          if (controls::param_slider_db(
                  mixer_slider, "##mixer_vol", ImVec2(22.0f, region_avail.y - 6.0f), &volume, db_range)) {
            track->set_volume(volume);
          }

          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("MIXER_VOLUME_CONTEXT_MENU");

          ImGui::SameLine();
          controls::level_meter(
              "##mixer_vu_meter", ImVec2(18.0f, region_avail.y - 6.0f), 2, track->level_meter, track->level_meter_color);
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("LEVEL_METER_MENU");
        }
        ImGui::EndGroup();
      }
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 0.0f);

    controls::vsplitter(id, avail.y, 2.0f);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PopID();
    id++;
  }

  /*
  int id = 0;
  for (auto track : Engine2::tracks) {
    float volume = track->ui_parameter_state.volume_db;
    float pan = track->ui_parameter_state.pan;
    bool mute = track->ui_parameter_state.mute;
    ImU32 color = track->color.to_uint32();

    ImGui::PushID(id);
    controls::mixer_label(track->name.c_str(), size.y, track->color);
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 6.0f));

    const float width = 48.0f;
    pan_knob.arc_color = color;
    if (controls::knob(pan_knob, "##pan_knob", ImVec2(width, 35.0f), &pan, pan_range))
      track->set_pan(pan);

    const float ms_btn_width = width * 0.5f - 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
    if (controls::toggle_button("M", &mute, muted_color, ImVec2(ms_btn_width, 0.0f)))
      track->set_mute(mute);

    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("S", ImVec2(ms_btn_width, 0.0f)))
      Engine2::solo_track(id);
    ImGui::PopStyleVar();

    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 2.0f));

    const ImVec2 region_avail = ImGui::GetContentRegionAvail();
    mixer_slider.grab_size.y = (region_avail.y < 200.0f) ? 22.0f : 28.0f;
    mixer_slider.pointer_color = color;
    //mixer_slider.grab_shade_color = track->color.brighten(0.25f).change_alpha(0.7f).premult_alpha().to_uint32();
    if (controls::param_slider_db(
            mixer_slider, "##mixer_vol", ImVec2(22.0f, region_avail.y - 6.0f), &volume, db_range)) {
      track->set_volume(volume);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
      ImGui::OpenPopup("MIXER_VOLUME_CONTEXT_MENU");

    ImGui::SameLine();
    controls::level_meter(
        "##mixer_vu_meter", ImVec2(18.0f, region_avail.y - 6.0f), 2, track->level_meter, track->level_meter_color);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
      ImGui::OpenPopup("LEVEL_METER_MENU");

    ImGui::EndGroup();

    if (ImGui::BeginPopup("MIXER_VOLUME_CONTEXT_MENU")) {
      if (ImGui::MenuItem("Reset Value"))
        track->set_volume(0.0f);
      ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("LEVEL_METER_MENU")) {
      ImGui::MenuItem("Color mode", nullptr, nullptr, false);
      ImGui::Separator();
      bool meter_color_normal = track->level_meter_color == LevelMeterColorMode::Normal;
      bool meter_color_line = track->level_meter_color == LevelMeterColorMode::Line;
      if (ImGui::MenuItem("Normal", nullptr, &meter_color_normal))
        track->level_meter_color = LevelMeterColorMode::Normal;
      if (ImGui::MenuItem("Line", nullptr, &meter_color_line))
        track->level_meter_color = LevelMeterColorMode::Line;
      ImGui::EndPopup();
    }

    ImGui::SameLine();

    ImGui::PopID();
    id++;
  }
  */

  ImGui::PopStyleVar();
  ImGui::PopStyleVar();

  controls::end_window();
}

MixerWindow g_mixer;

}  // namespace wb