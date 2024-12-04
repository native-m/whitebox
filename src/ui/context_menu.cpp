#include "context_menu.h"
#include "engine/audio_io.h"
#include "engine/track.h"

namespace wb {
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