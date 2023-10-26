#include "controls.h"

namespace wb::controls
{
    void vu_meter(const char* str_id, const ImVec2& size, int num_channels, const float* levels)
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

        /*for (int i = 0; i < num_channels; i++) {
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
        }*/

        draw_list->AddRect(ImVec2(cursor_pos.x - 1.0f, cursor_pos.y - 1.0f),
                           ImVec2(bb.Max.x + 1.0f, bb.Max.y + 1.0f),
                           ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border)));
    }
}