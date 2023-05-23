#pragma once

#include <concepts>
#include <cmath>

namespace wb
{
    template<typename T, typename T2>
    inline static T max(T a, T2 b)
    {
        return a;
    }

    template<typename T>
    inline static T round_n(const T value, const int decimal_places)
    {
        const T dec = std::pow(10, (const T)decimal_places);
        return std::round(value * dec) / dec;
    }
}