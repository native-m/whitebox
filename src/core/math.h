#pragma once

#include "common.h"
#include "types.h"
#include <cmath>

namespace wb {

namespace math {

template <typename T>
inline T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
inline T max(T a, T b) {
    return b < a ? a : b;
}

template <std::floating_point T>
inline T trunc(T x) {
    if constexpr (std::is_same_v<T, double>) {
        return (T)(int64_t)x;
    } else {
        return (T)(int32_t)x;
    }
}

template <std::floating_point T>
inline T uround(T x) {
    return math::trunc(x + T(0.5));
}

template <std::floating_point T>
inline T round(T x) {
    return math::trunc(x + (x > T(0) ? 0.5 : -0.5));
}

template <std::floating_point T>
inline T fract(T x) {
    return x - std::floor(x);
}

template <std::floating_point T>
inline T db_to_linear(T x) {
    return std::pow(T(10), T((double)x * 0.05));
}

template <std::floating_point T>
inline T linear_to_db(T x) {
    return T(20) * std::log10(std::abs(x));
}

} // namespace math

template <typename T>
inline bool is_pow_2(T x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

template <typename T>
inline bool in_range(T x, T min_val, T max_val) {
    return (x >= min_val) && (x <= max_val);
}

template <typename T>
inline bool is_multiple_of(T x, T mult) {
    return (x % mult) == 0;
}

template <typename T>
inline T clamp(T x, T min_val, T max_val) {
    T max_part = x < max_val ? x : max_val;
    return max_part > min_val ? max_part : min_val;
}

inline static double samples_to_beat(size_t samples, double sample_rate, double beat_duration) {
    double sec = (double)samples / sample_rate;
    return sec / beat_duration;
}

inline static double beat_to_samples(double beat, double sample_rate, double beat_duration) {
    double sec = beat * beat_duration;
    return (sec * sample_rate);
}

} // namespace wb