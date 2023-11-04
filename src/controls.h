#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include "types.h"
#include "stdpch.h"
#include "core/debug.h"
#include "core/color.h"
#include "core/unit_conversion.h"

namespace wb::controls
{
    enum class SliderGrabShape
    {
        Circle,
        Rectangle
    };

    enum class SliderScale
    {
        Linear,
        Logarithm
    };

    struct SliderProperties
    {
        SliderScale     scale           = SliderScale::Linear;
        SliderGrabShape grab_shape      = SliderGrabShape::Circle;
        ImVec2          grab_size       = {};
        float           grab_roundness  = 0.0f;
        float           frame_width     = 0.0f;
    };

    // Optimizes dockable window and disable its borders when docked
    // This should be ended with ImGui::End()
    static bool begin_dockable_window(const char* title, bool* p_open = nullptr, ImGuiWindowFlags flags = 0)
    {
        auto state_storage = ImGui::GetStateStorage();
        auto docked = state_storage->GetBoolRef(ImGui::GetID((const void*)title));
        float border_size = 1.0f;
        if (*docked) {
            flags |= ImGuiWindowFlags_NoBackground;
            border_size = 0.0f;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_size);
        bool ret = ImGui::Begin(title, p_open, flags);
        *docked = ret && ImGui::IsWindowDocked();
        ImGui::PopStyleVar();
        return ret;
    }

    static void knob()
    {
        
    }

    template<ArithmeticType T>
    static bool slider2(const SliderProperties& properties,
                        const char* str_id,
                        const ImVec2& size,
                        T* value, T min, T max)
    {
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImRect bb(cursor_pos, ImVec2(cursor_pos.x + size.x, cursor_pos.y + size.y));
        ImGuiID id = ImGui::GetID(str_id);

        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
        ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImVec2& mouse_pos = g.IO.MousePos;
        ImVec2 grab_size(properties.grab_size);
        ImU32 frame_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImU32 grab_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        float scroll_height = size.y - grab_size.y;
        float inv_scroll_height = 1.0f / scroll_height;
        float frame_width = std::max(properties.frame_width, 3.0f);
        bool dragging = held && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

        if (ImGui::IsItemActivated())
            g.SliderGrabClickOffset = mouse_pos.y - ((1.0f - *value) * scroll_height + cursor_pos.y);

        if (held) {
            float val = (mouse_pos.y - cursor_pos.y - g.SliderGrabClickOffset) * inv_scroll_height;
            *value = std::clamp(1.0f - val, 0.0f, 1.0f);
            //ImGui::SetNextWindowPos(ImVec2())
            ImGui::BeginTooltip();
            ImGui::Text("%.3f", *value);
            ImGui::EndTooltip();
        }

        float grab_pos = (1.0f - *value) * scroll_height;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 frame_rect_min(cursor_pos.x + size.x * 0.5f - frame_width * 0.5f, cursor_pos.y + grab_size.y * 0.5f);
        ImVec2 frame_rect_max(frame_rect_min.x + frame_width, frame_rect_min.y + scroll_height);
        ImVec2 grab_rect_min(cursor_pos.x + size.x * 0.5f - grab_size.x * 0.5f, cursor_pos.y + grab_pos);
        ImVec2 grab_rect_max(grab_rect_min.x + grab_size.x, grab_rect_min.y + grab_size.y);

        //draw_list->AddRect(cursor_pos, bb.Max, frame_col);
        draw_list->AddRectFilled(frame_rect_min, frame_rect_max, frame_col);
        draw_list->AddRectFilled(grab_rect_min, grab_rect_max, grab_col, properties.grab_roundness);
        draw_list->AddLine(ImVec2(grab_rect_min.x + 2.0f, grab_rect_min.y + grab_size.y * 0.5f),
                           ImVec2(grab_rect_min.x + grab_size.x - 2.0f, grab_rect_min.y + grab_size.y * 0.5f),
                           0xFFFFFFFF, 3.0f);

        return true;
    }

    void vu_meter(const char* str_id, const ImVec2& size, int num_channels, const float* levels);
}