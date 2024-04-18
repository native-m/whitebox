#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace wb {
void add_vertical_text(ImDrawList* DrawList, const char* text, ImVec2 pos, ImVec4 rect, ImU32 text_color);
} // namespace wb