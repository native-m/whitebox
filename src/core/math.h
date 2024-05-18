#pragma once

#include "common.h"
#include <cmath>

namespace wb {

namespace math {

inline double uround(double x) {
    double v = x + 0.5;
    return (double)((uint64_t)(v));
}

template <typename T>
inline T fract(T x) {
    return x - std::floor(x);
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