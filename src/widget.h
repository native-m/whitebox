#pragma once

#include "types.h"
#include "stdpch.h"
#include "core/debug.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace wb::widget
{
    inline static void vseparator_with_y_offset(float y_offset)
    {
        float cur_pos_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(cur_pos_y + y_offset);
        ImGui::Separator();
        ImGui::SetCursorPosY(cur_pos_y);
    }

    template<typename T>
    static bool hseparator_resizer(T id, float* size, float default_size, float min_size = 0.0f, float max_size = 0.0f)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
        ImVec2 region_avail = ImGui::GetContentRegionAvail();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImGuiCol color = ImGuiCol_Separator;
        const float separator_pad = 2.0f;

        ImGui::PushID(id);
        //ImGuiStorage* states = ImGui::GetStateStorage();

        ImGui::InvisibleButton("", ImVec2(region_avail.x, separator_pad));
        bool is_separator_active = ImGui::IsItemActive();

        if (size) {
            bool is_separator_hovered = ImGui::IsItemHovered();

            if (is_separator_hovered || is_separator_active) {
                if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    *size = default_size;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }

            if (is_separator_active) {
                //auto drag_pos = states->GetFloatRef(ImGui::GetID("separator_drag_pos"));
                auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
                float prev_size = *size;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                *size = std::clamp(*size + drag_delta.y, min_size, max_size);
                color = ImGuiCol_SeparatorActive;
            }
            else if (is_separator_hovered) {
                color = ImGuiCol_SeparatorHovered;
            }
        }

        draw_list->AddLine(ImVec2(cur_pos.x, cur_pos.y + 0.5f),
                           ImVec2(cur_pos.x + region_avail.x, cur_pos.y + 0.5f),
                           ImGui::GetColorU32(color),
                           2.0f);

        ImGui::PopID();
        return is_separator_active;
    }

    static bool collapse_button(const char* str_id, bool* shown)
    {
        auto draw_list = ImGui::GetWindowDrawList();
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiID id = ImGui::GetID(str_id);
        float font_size = ImGui::GetFontSize();
        float padding = style.FramePadding.x;
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
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
            draw_list->AddCircleFilled(ImVec2(cur_pos.x + (font_size + padding) * 0.5f, cur_pos.y + (font_size + padding) * 0.5f),
                                       font_size * 0.5f + 1.0f,
                                       bg_col);
        }

        ImGui::RenderArrow(draw_list,
                           ImVec2(cur_pos.x + padding * 0.5f, cur_pos.y + padding * 0.5f),
                           ImGui::GetColorU32(ImGuiCol_Text), *shown ? ImGuiDir_Down : ImGuiDir_Right);

        return pressed;
    }

    static void push_button(const char* label, bool* v, const ImVec2& size = ImVec2(0, 0), ImGuiButtonFlags flags = 0)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
        ImVec2 text_size = ImGui::CalcTextSize(label);
        ImVec2 frame_size = ImGui::CalcItemSize(size, text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);
        ImVec2 max = ImVec2(cur_pos.x + frame_size.x, cur_pos.y + frame_size.y);
        ImVec2 text_pos = ImVec2(cur_pos.x + frame_size.x * 0.5f - text_size.x * 0.5f, cur_pos.y + frame_size.y * 0.5f - text_size.y * 0.5f);

        if (ImGui::InvisibleButton(label, frame_size, flags)) {
            *v = !*v;
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        if (ImGui::IsItemActive()) {
            draw_list->AddRectFilled(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_ButtonActive]), style.FrameRounding);
        }
        else if (ImGui::IsItemHovered()) {
            if (*v) {
                draw_list->AddRectFilled(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_ButtonHovered]), style.FrameRounding);
            }
            else {
                draw_list->AddRect(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_ButtonHovered]), style.FrameRounding);
            }
        }
        else if (!*v) {
            draw_list->AddRect(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_Button]), style.FrameRounding);
        }
        else {
            draw_list->AddRectFilled(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_Button]), style.FrameRounding);
        }

        if (style.FrameBorderSize > 0.0f && *v) {
            draw_list->AddRect(cur_pos, max, ImGui::GetColorU32(style.Colors[ImGuiCol_Border]), style.FrameRounding);
        }

        draw_list->AddText(text_pos, ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), label);
    }

    template<typename T> requires std::integral<T> || std::floating_point<T>
    inline static bool drag(const char* label, T* data, float speed = 1.0f, std::type_identity_t<T> min = 0,
                            std::type_identity_t<T> max = 0, std::type_identity_t<T> default_value = 0,
                            const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
        bool ret;

        if constexpr (std::is_same_v<T, int8_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_S8, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int16_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_S16, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int32_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_S32, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_S64, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint8_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_U8, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint16_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_U16, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint32_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_U32, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_U64, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, float>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_Float, data, speed, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, double>) {
            ret = ImGui::DragScalar(label, ImGuiDataType_Double, data, speed, &min, &max, format, flags);
        }

        // Alt+Click to reset the value
        if (ImGui::IsItemClicked() && ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
            *data = std::clamp(default_value, min, max);
        }

        return ret;
    }

    template<typename T> requires std::integral<T> || std::floating_point<T>
    inline static bool slider(const char* label, T* data, std::type_identity_t<T> min = 0,
                              std::type_identity_t<T> max = 0, std::type_identity_t<T> default_value = 0,
                              const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
        bool ret;

        if constexpr (std::is_same_v<T, int8_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_S8, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int16_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_S16, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int32_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_S32, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_S64, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint8_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_U8, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint16_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_U16, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint32_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_U32, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_U64, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, float>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_Float, data, &min, &max, format, flags);
        }
        else if constexpr (std::is_same_v<T, double>) {
            ret = ImGui::SliderScalar(label, ImGuiDataType_Double, data, &min, &max, format, flags);
        }

        // Alt+Click to reset the value
        if (ImGui::IsItemClicked() && ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
            *data = std::clamp(default_value, min, max);
        }

        return ret;
    }

    template<typename T> requires std::integral<T> || std::floating_point<T>
    struct DragParam
    {
        T* data;
        T min;
        T max;
        T default_value = 0;
    };

    template<typename T, typename... Rest> requires All<T, Rest...>
    static bool drag_delimited_n(const char* label, const char* delimiter_str, bool label_at_left_side,
                                 float speed, ImGuiSliderFlags flags, const DragParam<T>& param,
                                 const DragParam<Rest>&... params)
    {
        static constexpr size_t num_params = sizeof...(params) + 1;
        ImGuiWindow* window = ImGui::GetCurrentWindowRead();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
        ImVec2 text_size = ImGui::CalcTextSize(label);
        ImVec2 delimit_size = ImGui::CalcTextSize(delimiter_str);
        //ImVec2 frame_size = ImGui::CalcItemSize(size, text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);
        ImVec2 text_pos = ImVec2(cur_pos.x, cur_pos.y + style.FramePadding.y);
        float delimit_pos_x = style.ItemSpacing.x * 0.5f + delimit_size.x * 0.5f;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        ImVec2 available_space = ImGui::GetContentRegionAvail();
        ImGuiDataType data_type = 0;

        if constexpr (std::is_same_v<T, int>) {
            data_type = ImGuiDataType_S32;
        }
        else if constexpr (std::is_same_v<T, float>) {
            data_type = ImGuiDataType_Float;
        }

        ImGui::BeginGroup();

        if (label_at_left_side) {
            draw_list->AddText(text_pos, ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), label);
            ImGui::SetCursorPosX(cur_pos.x + text_size.x + style.ItemSpacing.x);
        }

        ImGui::PushID(label);
        ImGui::PushMultiItemsWidths(num_params, ImGui::CalcItemWidth());

#define DRAG_APPEND_POS() ImVec2(window->DC.CursorPos.x - delimit_pos_x, window->DC.CursorPos.y + style.FramePadding.y)

        uint32_t id = 0;
        ImGui::PushID(id++);
        bool value_changed = drag("", param.data, speed, param.min, param.max, param.default_value, nullptr, flags);
        draw_list->AddText(DRAG_APPEND_POS(), ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), delimiter_str);
        ImGui::PopID();
        ImGui::PopItemWidth();

        // WTF??
        ((ImGui::PushID(id++),
          value_changed |= drag("", params.data, speed, params.min, params.max, params.default_value, nullptr, flags),
          ImGui::SameLine(0, style.ItemInnerSpacing.x),
          (id != num_params) ? draw_list->AddText(DRAG_APPEND_POS(), ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), delimiter_str) : ((void)0),
          ImGui::PopID(),
          ImGui::PopItemWidth()), ...);

#undef DRAG_APPEND_POS

        ImGui::PopID();
        ImGui::EndGroup();

        return value_changed;
    }
    
    void song_position(uint64_t* t);
}
