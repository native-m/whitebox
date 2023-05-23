#pragma once

#include "widget.h"

namespace daw
{
    static void test_window()
    {
        static int8_t int8_slider = 0;
        static int16_t int16_slider = 0;
        static int32_t int32_slider = 0;
        static int64_t int64_slider = 0;
        static float float_slider = 0.0f;
        static double double_slider = 0.0;

        ImGui::Begin("Test Window");
        widget::slider("int8_t", &int8_slider, 0, 100);
        widget::slider("int16_t", &int16_slider, 0, 100);
        widget::slider("int32_t", &int32_slider, 0, 100);
        widget::slider("int64_t", &int64_slider, 0, 100);
        widget::slider("float", &float_slider, 0.0f, 100.0f);
        widget::slider("double", &double_slider, 0.0, 100.0);
        ImGui::End();
    }
}
