#pragma once

#include <imgui.h>

namespace wb {

struct GridProperties {
  double max_grid_division;
  double gap_scale;
};

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
    const GridProperties& properties,
    float alpha,
    bool triplet = false);

}  // namespace wb
