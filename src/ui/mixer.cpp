#include "mixer.h"
#include "controls.h"
#include "core/debug.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {

void GuiMixer::render() {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);

    ImVec2 window_padding = GImGui->Style.WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));

    if (!controls::begin_dockable_window(
            "Mixer", &open, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Open mixer track state...");
            ImGui::MenuItem("Save mixer track state...");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    static controls::SliderProperties mixer_slider = {
        .grab_shape = controls::SliderGrabShape::Circle,
        .grab_size = {16.0f, 28.0f},
        .grab_roundness = 2.0f,
        .frame_width = 4.0f,
        .extra_padding = {0.0f, 4.0f},
    };

    ImVec2 size = ImGui::GetContentRegionAvail();

    // Log::info("{}", size.y);
    int id = 0;
    for (auto track : g_engine.tracks) {
        ImGui::PushID(id);
        controls::mixer_label(track->name.c_str(), size.y, track->color);
        ImGui::SameLine();

        /*bool param_updated = false;
        if (controls::param_slider_db(track->ui_parameter, TrackParameter_Volume, mixer_slider,
                                      "##mixer_slider", ImVec2(20.0f, size.y), track->color)) {
            param_updated = true;
        }*/

        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 10.0f));
        controls::vu_meter("##mixer_vu_meter", ImVec2(18.0f, size.y - 20.0f), 2, track->vu_meter);
        ImGui::SameLine();

        ImGui::PopID();
        id++;
    }
    ImGui::PopStyleVar();

    ImGui::PopStyleVar();

    ImGui::End();
}

GuiMixer g_mixer;

} // namespace wb