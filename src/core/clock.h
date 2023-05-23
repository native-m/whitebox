#pragma once

#include <chrono>

namespace wb
{
    inline static double get_current_microsecond_clock()
    {
        return std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
}