#include "mixer.h"

#include "controls.h"
#include "core/debug.h"
#include "engine/engine.h"
#include "engine/track.h"
#include "window.h"

namespace wb {

void MixerWindow::render() {
  ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);

  ImVec2 window_padding = GImGui->Style.WindowPadding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));

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
    ImGui::EndMenuBar();
  }

  ImVec2 size = ImGui::GetContentRegionAvail();
  const NonLinearRange db_range(-72.0f, 6.0f, -2.4f);
  const LinearRange pan_range{ -1.0f, 1.0f };
  const ImVec4 muted_color(0.951f, 0.322f, 0.322f, 1.000f);
  const controls::KnobProperties pan_knob = {
    .body_color = 0xFF505050,
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
    .grab_shape = controls::SliderGrabShape::Rectangle,
    .grab_size = { 16.0f, 28.0f },
    .grab_roundness = 2.0f,
    .extra_padding = { 0.0f, 4.0f },
    .frame_width = 4.0f,
    .with_default_value_tick = true,
  };

  // Log::info("{}", size.y);
  int id = 0;
  for (auto track : g_engine.tracks) {
    float volume = track->ui_parameter_state.volume_db;
    float pan = track->ui_parameter_state.pan;
    bool mute = track->ui_parameter_state.mute;

    ImGui::PushID(id);
    controls::mixer_label(track->name.c_str(), size.y, track->color);
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 6.0f));

    const float width = 48.0f;
    if (controls::knob(pan_knob, "##pan_knob", ImVec2(width, 35.0f), &pan, pan_range))
      track->set_pan(pan);

    const float ms_btn_width = width * 0.5f - 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
    if (controls::toggle_button("M", &mute, muted_color, ImVec2(ms_btn_width, 0.0f)))
      track->set_mute(mute);

    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("S", ImVec2(ms_btn_width, 0.0f)))
      g_engine.solo_track(id);
    ImGui::PopStyleVar();

    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 2.0f));

    const ImVec2 region_avail = ImGui::GetContentRegionAvail();
    mixer_slider.grab_size.y = (region_avail.y < 200.0f) ? 22.0f : 28.0f;
    if (controls::param_slider_db(
            mixer_slider, "##mixer_vol", ImVec2(22.0f, region_avail.y - 6.0f), track->color, &volume, db_range)) {
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

  ImGui::PopStyleVar();
  ImGui::PopStyleVar();

  controls::end_window();
}

MixerWindow g_mixer;

}  // namespace wb