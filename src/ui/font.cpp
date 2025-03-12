#include "font.h"

#include <imgui.h>
#include <imgui_freetype.h>

#include "core/common.h"
#include "ui/IconsMaterialSymbols.h"

namespace wb {

ImFont* g_fonts[3];

void init_font_assets() {
  static const ImWchar icons_ranges[] = { ICON_MIN_MS, ICON_MAX_MS, 0 };
  ImFontConfig config;
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
  config.SizePixels = 13.0f;
  config.OversampleV = 2.0f;
  // config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
  g_fonts[(uint32_t)FontType::Normal] = io.Fonts->AddFontFromFileTTF("assets/Inter-Regular.ttf", 0.0f, &config);
  config.GlyphExtraSpacing.x = 0.0f;
  config.FontBuilderFlags = 0;
  config.RasterizerDensity = 1.0f;
  config.SizePixels = 24.0f;
  config.GlyphExtraSpacing.x = 0.0f;
  config.GlyphOffset.y -= 1.0f;
  g_fonts[(uint32_t)FontType::MonoMedium] = io.Fonts->AddFontFromFileTTF("assets/RobotoMono-Regular.ttf", 0.0f, &config);
  config.SizePixels = 24.0f;
  config.GlyphOffset.y = 0.0f;
  config.FontBuilderFlags = 0;
  g_fonts[(uint32_t)FontType::Icon] =
      io.Fonts->AddFontFromFileTTF("assets/MaterialSymbolsRoundedInstanced.ttf", 0.0f, &config, icons_ranges);
  io.Fonts->Build();
}

void set_current_font(FontType type) {
  ImGui::SetCurrentFont(g_fonts[(uint32_t)type]);
}

}  // namespace wb