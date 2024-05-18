#include "font.h"
#include "core/common.h"
#include "ui/IconsMaterialSymbols.h"
#include <imgui.h>
#include <imgui_freetype.h>

namespace wb {

ImFont* g_fonts[3];

void set_current_font(FontType type) {
    ImGui::SetCurrentFont(g_fonts[(uint32_t)type]);
}

void init_font_assets() {
    static const ImWchar icons_ranges[] = {ICON_MIN_MS, ICON_MAX_MS, 0};
    ImFontConfig config;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    config.SizePixels = 13.0f;
    g_fonts[(uint32_t)FontType::Nornal] =
        io.Fonts->AddFontFromFileTTF("assets/Inter-Medium.otf", 0.0f, &config);
    config.SizePixels = 24.0f;
    config.GlyphOffset.y -= 1.0f;
    g_fonts[(uint32_t)FontType::MonoMedium] =
        io.Fonts->AddFontFromFileTTF("assets/RobotoMono-Regular.ttf", 0.0f, &config);
    config.SizePixels = 24.0f;
    config.GlyphOffset.y = 0.0f;
    g_fonts[(uint32_t)FontType::Icon] = io.Fonts->AddFontFromFileTTF(
        "assets/MaterialSymbolsRoundedInstanced.ttf", 0.0f, &config, icons_ranges);
    io.Fonts->Build();
}

} // namespace wb