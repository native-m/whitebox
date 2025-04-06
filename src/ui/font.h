#pragma once

namespace wb {
enum class FontType {
  Normal,
  MonoMedium,
  Icon,
};

void init_font_assets();
void set_current_font(FontType type);
}  // namespace wb