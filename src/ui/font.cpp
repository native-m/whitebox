#include "font.h"

#include <imgui.h>
#include <imgui_freetype.h>

#include "core/common.h"
#include "ui/IconsMaterialSymbols.h"

namespace wb {

static ImFont* font_collection_[3];

void init_font_assets() {
  static const ImWchar icons_ranges[] = { ICON_MIN_MS, ICON_MAX_MS, 0 };
  ImFontConfig config;
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->FontLoader = ImGuiFreeType::GetFontLoader();
  
  //config.FontLoaderFlags |= ImGuiFreeTypeBuilderFlags_NoHinting;
  font_collection_[(uint32_t)FontType::Normal] = io.Fonts->AddFontFromFileTTF("assets/Inter-Regular.ttf", 0.0f, &config);
  font_collection_[(uint32_t)FontType::MonoMedium] = io.Fonts->AddFontFromFileTTF("assets/RobotoMono-Regular.ttf");
  font_collection_[(uint32_t)FontType::Icon] = io.Fonts->AddFontFromFileTTF("assets/MaterialSymbolsSharp_Filled-Regular.ttf");

  /*
  config.SizePixels = 13.0f;
  config.OversampleV = 2.0f;
  // config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
  g_fonts[(uint32_t)FontType::Normal] = io.Fonts->AddFontFromFileTTF("assets/Inter-Regular.ttf", 0.0f, &config);
  config.FontBuilderFlags = 0;
  config.RasterizerDensity = 1.0f;
  config.SizePixels = 24.0f;
  config.GlyphOffset.y -= 1.0f;
  g_fonts[(uint32_t)FontType::MonoMedium] = io.Fonts->AddFontFromFileTTF("assets/RobotoMono-Regular.ttf", 0.0f, &config);
  config.SizePixels = 24.0f;
  config.GlyphOffset.y = 0.0f;
  config.FontBuilderFlags = 0;
  g_fonts[(uint32_t)FontType::Icon] =
      io.Fonts->AddFontFromFileTTF("assets/MaterialSymbolsSharp_Filled-Regular.ttf", 0.0f, &config, icons_ranges);
  io.Fonts->Build();
  */
}

void font_push(FontType type, float font_base_size) {
  ImFont* font = font_collection_[(uint32_t)type];
  ImGui::PushFont(font, font_base_size);
}

void font_pop() {
  ImGui::PopFont();
}

}  // namespace wb