#pragma once

#include "popup_state_manager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <bit>

namespace wb::controls {

static bool begin_dockable_window(const char* title, bool* p_open = nullptr,
                                  ImGuiWindowFlags flags = 0) {
    auto state_storage = ImGui::GetStateStorage();
    auto hide_background = state_storage->GetBoolRef(ImGui::GetID((const void*)title));
    float border_size = 1.0f;
    if (*hide_background) {
        flags |= ImGuiWindowFlags_NoBackground;
        border_size = 0.0f;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_size);
    bool ret = ImGui::Begin(title, p_open, flags);
    ImGuiDockNode* node = ImGui::GetWindowDockNode();
    if (ret && node) {
        if (node->HostWindow) {
            // Don't draw background when the background is already drawn by the host window
            *hide_background = (node->HostWindow->Flags & ImGuiWindowFlags_NoBackground) ==
                               ImGuiWindowFlags_NoBackground;
        } else {
            *hide_background = false;
        }
    } else {
        *hide_background = false;
    }
    ImGui::PopStyleVar();
    return ret;
}

static bool collapse_button(const char* str_id, bool* shown) {
    ImGuiID id = ImGui::GetID(str_id);
    ImGuiStyle& style = ImGui::GetStyle();
    float font_size = ImGui::GetFontSize();
    auto draw_list = ImGui::GetWindowDrawList();
    ImVec2 cur_pos = ImGui::GetCursorScreenPos();
    float padding = style.FramePadding.x;
    cur_pos.y += style.FramePadding.y * 0.5f;
    ImRect bb(cur_pos, ImVec2(cur_pos.x + font_size + padding, cur_pos.y + font_size + padding));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    if (pressed)
        *shown = !*shown;

    if (hovered || held) {
        ImU32 bg_col = ImGui::GetColorU32(!held ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive);
        draw_list->AddCircleFilled(ImVec2(cur_pos.x + (font_size + padding) * 0.5f,
                                          cur_pos.y + (font_size + padding) * 0.5f),
                                   font_size * 0.5f + 1.0f, bg_col);
    }

    ImGui::RenderArrow(draw_list, ImVec2(cur_pos.x + padding * 0.5f, cur_pos.y + padding * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text), *shown ? ImGuiDir_Down : ImGuiDir_Right);

    // draw_list->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_PlotLines));

    return pressed;
}

template <typename T>
static bool hseparator_resizer(T id, float* size, float default_size, float min_size = 0.0f,
                               float max_size = 0.0f) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImVec2 region_avail = ImGui::GetWindowContentRegionMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cur_pos = ImGui::GetCursorScreenPos();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiCol color = ImGuiCol_Separator;
    const float separator_pad = 2.0f;

    ImGui::PushID(id);

    ImGuiID item_id = window->GetID("");
    ImRect bb(cur_pos, ImVec2(cur_pos.x + region_avail.x, cur_pos.y + separator_pad));
    ImGui::ItemSize(ImVec2(region_avail.x, separator_pad));
    if (!ImGui::ItemAdd(bb, item_id)) {
        ImGui::PopID();
        return false;
    }

    ImGui::ButtonBehavior(bb, item_id, nullptr, nullptr, 0);
    bool is_separator_active = ImGui::IsItemActive();

    if (size) {
        bool is_separator_hovered = ImGui::IsItemHovered();

        if (is_separator_hovered || is_separator_active) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                *size = default_size;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        if (is_separator_active) {
            // auto drag_pos = states->GetFloatRef(ImGui::GetID("separator_drag_pos"));
            auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
            float prev_size = *size;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            *size = std::clamp(*size + drag_delta.y, min_size, max_size);
            color = ImGuiCol_SeparatorActive;
        } else if (is_separator_hovered) {
            color = ImGuiCol_SeparatorHovered;
        }
    }

    draw_list->AddLine(ImVec2(cur_pos.x, cur_pos.y + 0.5f),
                       ImVec2(cur_pos.x + region_avail.x, cur_pos.y + 0.5f),
                       ImGui::GetColorU32(color), 2.0f);

    ImGui::PopID();
    return is_separator_active;
}

} // namespace wb::controls