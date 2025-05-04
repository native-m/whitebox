#pragma once

#include <imgui.h>

namespace wb {

void draw_musical_guidestripes(
    ImDrawList* dl,
    const ImVec2& pos,
    const ImVec2& size,
    double scroll_pos_x,
    double scale,
    float alpha = 0.12f);

void draw_musical_grid(
    ImDrawList* dl,
    const ImVec2& pos,
    const ImVec2& size,
    double scroll_pos_x,
    double length_per_beat,
    double max_grid_division,
    float alpha,
    bool triplet = false);

}  // namespace wb
