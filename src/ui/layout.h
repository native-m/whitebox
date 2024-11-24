#pragma once

#include <imgui.h>

namespace wb {
enum class LayoutPosition {
    Relative,
    Fixed,
};

struct Layout {
    ImVec2 main_pos;
    ImVec2 current_pos;
    float scroll_y;
    LayoutPosition current_layout_pos {};

    inline Layout() : main_pos(ImGui::GetCursorScreenPos()), current_pos(main_pos), scroll_y(ImGui::GetScrollY()) {}

    inline ImVec2 next(LayoutPosition position = LayoutPosition::Relative, ImVec2 offset = ImVec2()) {
        ImVec2 pos;

        switch (position) {
            case LayoutPosition::Relative:
                break;
            case LayoutPosition::Fixed:
                pos = ImVec2(main_pos.x + offset.x, main_pos.y + scroll_y + offset.y);
                break;
        }

        current_layout_pos = position;
        ImGui::SetCursorScreenPos(pos);
        return pos;
    }

    inline void end() {
        if (current_layout_pos == LayoutPosition::Fixed)
            ImGui::SetCursorScreenPos(main_pos);
    }
};
} // namespace wb