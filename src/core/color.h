#pragma once

//#include <imgui.h>

#include "core_math.h"

#define WB_IM_COLOR_U32_SET_ALPHA(col, alpha) (((col) & 0x00FFFFFFu) | (alpha << 24u))
#define WB_IM_COLOR_U32_GET_ALPHA(col)        (((col) & 0xFF000000u) >> 24u)
#define WB_COLOR_U32_R_SHIFT                  0
#define WB_COLOR_U32_G_SHIFT                  8
#define WB_COLOR_U32_B_SHIFT                  16
#define WB_COLOR_U32_A_SHIFT                  24
#define WB_COLOR_U32_SET_R(r)                 ((r) << WB_COLOR_U32_R_SHIFT)
#define WB_COLOR_U32_SET_G(g)                 ((g) << WB_COLOR_U32_G_SHIFT)
#define WB_COLOR_U32_SET_B(b)                 ((b) << WB_COLOR_U32_B_SHIFT)
#define WB_COLOR_U32_SET_A(a)                 ((a) << WB_COLOR_U32_A_SHIFT)
#define WB_COLOR_U32_GET_R(col)               (((col) >> WB_COLOR_U32_R_SHIFT) & 0xFFu)
#define WB_COLOR_U32_GET_G(col)               (((col) >> WB_COLOR_U32_G_SHIFT) & 0xFFu)
#define WB_COLOR_U32_GET_B(col)               (((col) >> WB_COLOR_U32_B_SHIFT) & 0xFFu)
#define WB_COLOR_U32_GET_A(col)               (((col) >> WB_COLOR_U32_A_SHIFT) & 0xFFu)
#define WB_COLOR_U32(r, g, b, a) \
  (WB_COLOR_U32_SET_R(r) | WB_COLOR_U32_SET_G(g) | WB_COLOR_U32_SET_B(b) | WB_COLOR_U32_SET_A(a))

struct ImVec4;

namespace wb {

using ColorU32 = uint32_t;

struct Color {
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

  constexpr Color() {
  }

  constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {
  }

  constexpr Color(int r, int g, int b, int a = 255)
      : r((float)r * (1.0 / 255.0f)),
        g((float)g * (1.0 / 255.0f)),
        b((float)b * (1.0 / 255.0f)),
        a((float)a * (1.0 / 255.0f)) {
  }

  constexpr Color(ColorU32 rgba)
      : r((float)WB_COLOR_U32_GET_R(rgba) * (1.0 / 255.0f)),
        g((float)WB_COLOR_U32_GET_G(rgba) * (1.0 / 255.0f)),
        b((float)WB_COLOR_U32_GET_B(rgba) * (1.0 / 255.0f)),
        a((float)WB_COLOR_U32_GET_A(rgba) * (1.0 / 255.0f)) {
  }

  Color(const ImVec4& vec4);

  inline constexpr float luminance() const {
    return (0.2126f * r) + (0.7152f * g) + (0.0722f * b);
  }

  inline constexpr Color darken(float amount) const {
    amount = 1.0f / (1.0f + amount);
    return Color(r * amount, g * amount, b * amount, a);
  }

  inline constexpr Color brighten(float amount) const {
    amount = 1.0f / (1.0f + amount);
    float new_r = 1.0f - (1.0f - r) * amount;
    float new_g = 1.0f - (1.0f - g) * amount;
    float new_b = 1.0f - (1.0f - b) * amount;
    return Color(new_r, new_g, new_b, a);
  }

  inline constexpr Color add_contrast(float amount) const {
    float new_r = math::saturate((r - 0.5f) * amount + 0.5f);
    float new_g = math::saturate((g - 0.5f) * amount + 0.5f);
    float new_b = math::saturate((b - 0.5f) * amount + 0.5f);
    return Color(new_r, new_g, new_b, a);
  }

  inline constexpr Color desaturate(float amount) const {
    const float y = luminance();
    return Color(r + amount * (y - r), g + amount * (y - g), b + amount * (y - b));
  }

  inline constexpr Color mix(const Color& other, float v) {
    return Color(math::lerp(r, other.r, v), math::lerp(g, other.g, v), math::lerp(b, other.b, v), math::lerp(a, other.a, v));
  }

  inline constexpr Color greyscale() {
    float lum = (0.2126f * r) + (0.7152f * g) + (0.0722f * b);
    return Color(lum, lum, lum, a);
  }

  inline constexpr Color premult_alpha() const {
    return Color(r * a, g * a, b * a, 1.0f);
  }

  inline constexpr Color change_alpha(float alpha) const {
    return Color(r, g, b, alpha);
  }

  inline constexpr Color make_opaque() const {
    return Color(r, g, b, 1.0f);
  }

  inline constexpr ColorU32 to_uint32() const {
    return WB_COLOR_U32(
        (uint32_t)(math::saturate(r) * 255.0f + 0.5f),
        (uint32_t)(math::saturate(g) * 255.0f + 0.5f),
        (uint32_t)(math::saturate(b) * 255.0f + 0.5f),
        (uint32_t)(math::saturate(a) * 255.0f + 0.5f));
  }

  static constexpr Color from_hsv(float h, float s, float v) {
    float out_r, out_g, out_b;

    if (s == 0.0f) {
      return Color(v, v, v, 1.0f);
    }

    h = math::fract(h) / (60.0f / 360.0f);
    int i = (int)h;
    float f = h - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    switch (i) {
      case 0:
        out_r = v;
        out_g = t;
        out_b = p;
        break;
      case 1:
        out_r = q;
        out_g = v;
        out_b = p;
        break;
      case 2:
        out_r = p;
        out_g = v;
        out_b = t;
        break;
      case 3:
        out_r = p;
        out_g = q;
        out_b = v;
        break;
      case 4:
        out_r = t;
        out_g = p;
        out_b = v;
        break;
      case 5:
      default:
        out_r = v;
        out_g = p;
        out_b = q;
        break;
    }

    return Color(out_r, out_g, out_b, 1.0f);
  }
};

inline static constexpr float calc_contrast_ratio(const Color& a, const Color& b) {
  float y1 = a.luminance();
  float y2 = b.luminance();
  return y1 > y2 ? (y2 + 0.05f) / (y1 + 0.05f) : (y1 + 0.05f) / (y2 + 0.05f);
}

}  // namespace wb