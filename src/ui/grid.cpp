#include "grid.h"

#include "core/color.h"
#include "core/debug.h"

namespace wb {

double calc_bar_division(double length_per_beat, double gap_scale, bool triplet) {
  const double division = std::exp2(std::round(std::log2(length_per_beat / gap_scale)));
  const double div_scale = triplet && (division >= 1.0) ? 3.0 : 2.0;
  return division * div_scale;
}

void draw_musical_guidestripes(
    ImDrawList* dl,
    const ImVec2& pos,
    const ImVec2& size,
    double scroll_pos_x,
    double view_scale,
    float alpha) {
  const ImU32 guidestrip_color = Color(ImGui::GetColorU32(ImGuiCol_Separator)).change_alpha(alpha).to_uint32();
  const double four_bars_length = 16.0 / view_scale;
  const uint32_t guidestrip_count = (uint32_t)(size.x / four_bars_length) + 2;
  float guidestrip_pos_x = pos.x - (float)std::fmod(scroll_pos_x, four_bars_length * 2.0);
  for (uint32_t i = 0; i <= guidestrip_count; i++) {
    float start_pos_x = guidestrip_pos_x;
    guidestrip_pos_x += four_bars_length;
    if (i % 2) {
      dl->AddRectFilled(ImVec2(start_pos_x, pos.y), ImVec2(guidestrip_pos_x, pos.y + size.y), guidestrip_color);
    }
  }
}

void draw_musical_grid(
    ImDrawList* dl,
    const ImVec2& pos,
    const ImVec2& size,
    double scroll_pos_x,
    double length_per_beat,
    const GridProperties& properties,
    float alpha,
    bool triplet) {
  static constexpr float beat_line_alpha = 0.28f;
  static constexpr float bar_line_alpha = 0.5f;
  const ImU32 line_color = ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 base_line_color = Color(line_color).multiply_alpha(alpha).to_uint32();
  const ImU32 beat_line_color = Color(line_color).change_alpha(beat_line_alpha * alpha).to_uint32();
  const ImU32 bar_line_color = Color(line_color).change_alpha(bar_line_alpha * alpha).to_uint32();
  const double beat = length_per_beat;
  const double bar = 4.0 * beat;
  const double division = std::exp2(std::round(std::log2(beat / properties.gap_scale)));
  const double max_division = math::min(division, properties.max_division * 0.5);
  const double div_scale = triplet && (max_division >= 1.0) ? 3.0 : 2.0;
  const double grid_inc_x = bar / (max_division * div_scale);
  const double inv_grid_inc_x = 1.0 / grid_inc_x;
  const uint32_t lines_per_bar = math::max((uint32_t)(bar / grid_inc_x), 1u);
  const uint32_t lines_per_beat = math::max((uint32_t)(beat / grid_inc_x), 1u);
  const uint32_t gridline_count = (uint32_t)((double)size.x * inv_grid_inc_x);
  const uint32_t count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);
  double line_pos_x = (double)pos.x - std::fmod(scroll_pos_x, grid_inc_x);
  float line_end_y = pos.y + size.y;
  for (uint32_t i = 0; i <= gridline_count; i++) {
    line_pos_x += grid_inc_x;
    float line_pixel_pos_x = (float)math::round(line_pos_x);
    uint32_t grid_id = i + count_offset + 1u;
    ImU32 line_color = base_line_color;
    if (grid_id % lines_per_bar) {
      line_color = bar_line_color;
    }
    if (grid_id % lines_per_beat) {
      line_color = beat_line_color;
    }
    dl->AddLine(ImVec2(line_pixel_pos_x, pos.y), ImVec2(line_pixel_pos_x, line_end_y), line_color, 1.0f);
  }
}

}  // namespace wb