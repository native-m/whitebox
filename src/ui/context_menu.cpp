#include "context_menu.h"

#include <imgui.h>

#include "engine/audio_io.h"
#include "engine/command_manager2.h"
#include "engine/engine2.h"
#include "engine/track.h"
#include "engine/track_command.h"
#include "forms.h"
#include "window_manager.h"

namespace wb {

TrackContextMenuResult
track_context_menu(Track* track, int32_t track_id, const std::string* tmp_name, const Color* tmp_color) {
  TrackContextMenuResult ret{};
  if (track->name.size() > 0) {
    ImGui::MenuItem(track->name.c_str(), nullptr, false, false);
  } else {
    ImGui::MenuItem("(unnamed)", nullptr, false, false);
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Rename")) {
    ret = TrackContextMenuResult::Rename;
    ImGui::CloseCurrentPopup();
  }

  if (ImGui::MenuItem("Change color")) {
    ret = TrackContextMenuResult::ChangeColor;
    ImGui::CloseCurrentPopup();
  }

  if (ImGui::MenuItem("Apply track color to every clip")) {
    CmdApplyTrackColorToClips* cmd = new CmdApplyTrackColorToClips();
    cmd->track_ids.push_back(track_id);
    CommandManager2::execute_command("Apply track color to every clip", cmd);
    ret = TrackContextMenuResult::Done;
  }

  ImGui::BeginDisabled(Engine2::is_recording());
  if (ImGui::MenuItem("Delete")) {
    CmdDeleteTrack* cmd = new CmdDeleteTrack();
    cmd->track_ids.push_back(track_id);
    CommandManager2::execute_command("Delete track", cmd);
    ret = TrackContextMenuResult::Done;
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  if (ImGui::MenuItem("Reset height")) {
    ImGui::CloseCurrentPopup();
    track->height = 60.0f;
    ret = TrackContextMenuResult::Done;
  }

  return ret;
}

void track_input_context_menu(Track* track, uint32_t track_slot) {
  uint32_t max_audio_input_channels = Engine2::current_engine_config.num_input_channels;
  bool none = track->input.type == TrackInputType::None;
  bool ext_stereo = track->input.type == TrackInputType::ExternalStereo;
  bool ext_mono = track->input.type == TrackInputType::ExternalMono;

  if (ImGui::Selectable("None", none, none ? ImGuiSelectableFlags_Highlight : 0))
    Engine2::set_track_input(track, TrackInputType::None, 0, track->input_attr.armed);

  ImGui::Selectable("Ext. stereo", true, ImGuiSelectableFlags_Disabled);
  for (uint32_t i = 0; i < max_audio_input_channels; i += 2) {
    const char* name;
    bool selected = ext_stereo && track->input.index == i;
    ImFormatStringToTempBuffer(&name, nullptr, "%d+%d", i + 1, i + 2);
    if (ImGui::Selectable(name, false, selected ? ImGuiSelectableFlags_Highlight : 0))
      Engine2::set_track_input(track, TrackInputType::ExternalStereo, i, track->input_attr.armed);
  }

  ImGui::Selectable("Ext. mono", true, ImGuiSelectableFlags_Disabled);
  for (uint32_t i = 0; i < max_audio_input_channels; i++) {
    const char* name;
    bool selected = ext_mono && track->input.index == i;
    ImFormatStringToTempBuffer(&name, nullptr, "%d", i + 1);
    if (ImGui::Selectable(name, false, selected ? ImGuiSelectableFlags_Highlight : 0))
      Engine2::set_track_input(track, TrackInputType::ExternalMono, i, track->input_attr.armed);
  }
}

void track_plugin_context_menu(Track* track) {
  if (ImGui::MenuItem("Open plugin editor", nullptr, nullptr, track->plugin_instance != nullptr)) {
    if (!track->plugin_instance->has_window_attached())
      wm_add_foreign_plugin_window(track->plugin_instance);
  }
  if (ImGui::MenuItem("Close plugin", nullptr, nullptr, track->plugin_instance != nullptr)) {
    if (track->plugin_instance->has_window_attached())
      wm_close_plugin_window(track->plugin_instance);
    Engine2::remove_plugin(track, 0);
  }
}

}  // namespace wb