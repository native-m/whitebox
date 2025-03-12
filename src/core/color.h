#pragma once

#include <imgui.h>

#include "core_math.h"

namespace wb {

inline static constexpr ImColor color_darken(const ImColor& color, float amount) {
  amount = 1.0f / (1.0f + amount);
  return ImColor(color.Value.x * amount, color.Value.y * amount, color.Value.z * amount);
}

inline static constexpr ImColor color_brighten(const ImColor& color, float amount) {
  amount = 1.0f / (1.0f + amount);
  return ImColor(
      1.0f - (1.0f - color.Value.x) * amount,
      1.0f - (1.0f - color.Value.y) * amount,
      1.0f - (1.0f - color.Value.z) * amount,
      color.Value.w);
}

inline static constexpr ImColor color_adjust_contrast(const ImColor& color, float amount) {
  return ImColor(
      (color.Value.x - 0.5f) * amount + 0.5f,
      (color.Value.y - 0.5f) * amount + 0.5f,
      (color.Value.z - 0.5f) * amount + 0.5f,
      color.Value.w);
}

inline static constexpr ImColor color_adjust_alpha(const ImColor& color, float alpha) {
  return ImColor(color.Value.x, color.Value.y, color.Value.z, alpha);
}

inline static constexpr ImColor color_premul_alpha(const ImColor& color) {
  return ImColor(color.Value.x * color.Value.w, color.Value.y * color.Value.w, color.Value.z * color.Value.w, 1.0f);
}

inline static constexpr ImColor color_mix(const ImColor& a, const ImColor& b, float t) {
  return ImColor(
      math::lerp(a.Value.x, b.Value.x, t),
      math::lerp(a.Value.y, b.Value.y, t),
      math::lerp(a.Value.z, b.Value.z, t),
      math::lerp(a.Value.w, b.Value.w, t));
}

inline static constexpr float color_luminance(const ImColor& color) {
  return (0.2126f * color.Value.x) + (0.7152f * color.Value.y) + (0.0722f * color.Value.z);
}

inline static constexpr float calc_contrast_ratio(const ImColor& a, const ImColor& b) {
  float y1 = color_luminance(a);
  float y2 = color_luminance(b);
  return y1 > y2 ? (y2 + 0.05f) / (y1 + 0.05f) : (y1 + 0.05f) / (y2 + 0.05f);
}

}  // namespace wb