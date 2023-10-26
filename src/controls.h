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

    struct SliderProperties
    {
        SliderGrabShape grab_shape      = SliderGrabShape::Circle;
        ImVec2          grab_size       = {};
        float           grab_roundness  = 0.0f;
        float           frame_width     = 0.0f;
    };

    static void vu_meter(const char* str_id, const ImVec2& size, int num_channels, const float* levels)
    {
        static const float max_full_scale = 6.0f;
        static const float loud = 0.0f;
        static const float moderate = -6.0;
        static const float normal = -18.0;
        static const float quiet = -70.0;
        static const float inv_max_full_scale = 1.0f / (max_full_scale - quiet);
        static const ImU32 loud_color = color_brighten(ImColor(1.0f, 0.0f, 0.0f), 0.5f);
        static const ImU32 moderate_color = color_brighten(ImColor(1.0f, 1.0f, 0.0f), 0.5f);
        static const ImU32 normal_color = color_brighten(ImColor(0.0f, 1.0f, 0.0f), 0.5f);
        static const ImU32 quiet_color = color_darken(ImColor(0.0f, 1.0f, 0.0f), 0.625f);

        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImRect bb(cursor_pos, ImVec2(cursor_pos.x + size.x, cursor_pos.y + size.y));
        ImGuiID id = ImGui::GetID(str_id);

        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImU32 vu_col = color_brighten(ImColor(0.0f, 1.0f, 0.0f), 0.625f);
        float level_width_per_ch = (bb.Max.x - bb.Min.x) / (float)num_channels;
        float pos_x = cursor_pos.x;

#if 0
        for (int i = 0; i < num_channels; i++) {
            static constexpr float level_padding = 1.0f;
            float level = std::min(gain_to_dbfs(std::abs(levels[i])), max_full_scale);
            float min_x = pos_x + level_padding;
            float max_x = pos_x + level_width_per_ch - level_padding;

            if (level > quiet) {
                float current_level = std::clamp(level, quiet, normal) * inv_max_full_scale;
                draw_list->AddRectFilled(ImVec2(min_x, bb.Max.y - current_level * size.y),
                                         ImVec2(max_x, bb.Max.y),
                                         quiet_color);
            }

            if (level > normal) {
                float current_level = std::clamp(level, normal, moderate) * inv_max_full_scale;
                draw_list->AddRectFilled(ImVec2(min_x, bb.Max.y - current_level * size.y),
                                         ImVec2(max_x, bb.Max.y - normal * inv_max_full_scale * size.y),
                                         normal_color);
            }

            if (level > moderate) {
                float current_level = std::clamp(level, moderate, loud) * inv_max_full_scale;
                draw_list->AddRectFilled(ImVec2(min_x, bb.Max.y - current_level * size.y),
                                         ImVec2(max_x, bb.Max.y - moderate * inv_max_full_scale * size.y),
                                         moderate_color);
            }

            if (level > loud) {
                float current_level = std::clamp(level, loud, max_full_scale) * inv_max_full_scale;
                draw_list->AddRectFilled(ImVec2(min_x, bb.Max.y - current_level * size.y),
                                         ImVec2(max_x, bb.Max.y - loud * inv_max_full_scale * size.y),
                                         loud_color);
            }
            
            pos_x += level_width_per_ch;
        }
#endif

        draw_list->AddRect(ImVec2(cursor_pos.x - 1.0f, cursor_pos.y - 1.0f),
                           ImVec2(bb.Max.x + 1.0f, bb.Max.y + 1.0f),
                           ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border)));
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
}