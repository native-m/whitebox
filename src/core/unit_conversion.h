#pragma once

#include "../types.h"
#include "../stdpch.h"

namespace wb
{
    template<std::floating_point T>
    inline static constexpr T dbfs_to_gain(T decibels)
    {
        static constexpr T minus_inf = -std::numeric_limits<T>::infinity();
        return decibels > minus_inf ? std::pow(T(10.0), decibels * T(0.05)) : T(0.0);
    }

    template<std::floating_point T>
    inline static constexpr T gain_to_dbfs(T gain)
    {
        static constexpr T minus_inf = -std::numeric_limits<T>::infinity();
        return gain > T(0.0) ? std::max(minus_inf, std::log10(gain) * T(20.0)) : minus_inf;
    }
}