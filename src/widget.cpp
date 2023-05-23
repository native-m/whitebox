#include "widget.h"

namespace wb::widget
{
    void song_position(uint64_t* t)
    {
        static int bar = 0;
        static int step = 0;
        static int tick = 0;

        ImVec2 item_spacing = ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing);
        ImGui::PushItemWidth(ImGui::GetFontSize() * 2.0f);
        ImGui::DragInt("##bar", &bar, 0.5f, 0, 100, "%d", ImGuiSliderFlags_Vertical);
        ImGui::TextEx(".");
        ImGui::DragInt("##step", &step, 0.5f, 0, 100, "%d", ImGuiSliderFlags_Vertical);
        ImGui::TextEx(".");
        ImGui::PopStyleVar();
        ImGui::DragInt("##tick", &tick, 0.5f, 0, 100, "%d", ImGuiSliderFlags_Vertical);
        ImGui::PopItemWidth();
    }
}