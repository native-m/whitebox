#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace wb {
ImVec2 draw_simple_text(ImDrawList* draw_list, const char* text, ImVec2 pos, ImU32 text_color);
void draw_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect,
                        ImU32 text_color);

} // namespace wb