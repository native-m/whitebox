#pragma once

namespace wb {
enum class FontType {
    Nornal,
    Medium,
    Icon,
};

void set_current_font(FontType type);
void init_font_assets();
} // namespace wb