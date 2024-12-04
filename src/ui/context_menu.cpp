#include "context_menu.h"
#include "forms.h"
#include "engine/audio_io.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {
bool track_context_menu(Track* track, int track_id, const std::string* tmp_name, const ImColor* tmp_color) {
    bool ret = false;
    if (track->name.size() > 0) {
        ImGui::MenuItem(track->name.c_str(), nullptr, false, false);
    } else {
        ImGui::MenuItem("(unnamed)", nullptr, false, false);
    }
    ImGui::Separator();

    if (ImGui::BeginMenu("Rename")) {
        FormResult result = rename_form(&track->name, tmp_name);
        if (result == FormResult::Close) {
            ImGui::CloseCurrentPopup();
            ret = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Change color")) {
        FormResult result = color_picker_form(&track->color, *tmp_color);
        switch (result) {
            case FormResult::ValueChanged:
                ret = true;
                break;
            case FormResult::Close:
                ImGui::CloseCurrentPopup();
                ret = true;
                break;
            default:
                break;
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Apply track color to every clip")) {
        for (auto clip : track->clips)
            clip->color = track->color;
        ret = true;
    }

    if (ImGui::MenuItem("Delete")) {
        g_engine.delete_track((uint32_t)track_id);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Reset height")) {
        ImGui::CloseCurrentPopup();
        track->height = 60.0f;
        ret = true;
    }

    return ret; 
}

void track_input_context_menu(Track* track) {
    uint32_t max_audio_input_channels = g_audio_io->max_input_channel_count;
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
}
} // namespace wb