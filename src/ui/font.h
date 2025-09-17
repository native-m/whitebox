#pragma once

namespace wb {
enum class FontType {
  Normal,
  MonoMedium,
  Icon,
};

void init_font_assets();
void font_push(FontType type, float font_base_size);
void font_pop();
}  // namespace wb