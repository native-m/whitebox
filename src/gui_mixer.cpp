#include "gui_mixer.h"
#include "controls.h"
#include <imgui.h>

namespace wb
{
    GUIMixer g_gui_mixer;

    void GUIMixer::render()
    {
        static double time = 0.0f;
        static float volume = 1.0f;
        static controls::SliderProperties mixer_slider = {
            .grab_shape = controls::SliderGrabShape::Circle,
            .grab_size = { 16.0f, 28.0f },
            .grab_roundness = 2.0f,
            .frame_width = 4.0f
        };

        if (!shown) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300));
        if (!ImGui::Begin("Mixer", &shown)) {
            ImGui::End();
            return;
        }

        ImGui::SmallButton("M");
        ImGui::SameLine();
        ImGui::SmallButton("S");

        float value = (float)std::cos(time * 2.0);
        controls::vu_meter("##test_vu", ImVec2(20.0f, 200.0f), 1, &value);
        ImGui::SameLine(0.0f, 4.0f);
        controls::slider2<float>(mixer_slider, "##test", ImVec2(20.0f, 200.0f), &volume, 0.0f, 1.0f);

        ImGui::End();

        time += ImGui::GetIO().DeltaTime;
    }
}