#pragma once

#include "common.h"
#include "types.h"
#include <cmath>

namespace wb {

namespace math {

template <std::floating_point T>
static constexpr T small_value = [] {
    if constexpr (std::same_as<T, float>)
        return 0.000001f;
    else
        return 0.000000000000001f;
}();

inline auto abs(NumericalType auto x) {
    return x < 0 ? -x : x;
}

template <typename T>
inline constexpr T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
inline constexpr T max(T a, T b) {
    return b < a ? a : b;
}

template <typename T>
inline T clamp(T x, T min_val, T max_val) {
    T max_part = x < max_val ? x : max_val;
    return max_part > min_val ? max_part : min_val;
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
    return math::trunc(x + (x < T(0) ? -0.5 : 0.5));
}

template <std::floating_point T>
inline T fract(T x) {
    return x - std::floor(x);
}

template <std::floating_point T>
inline T exponential_ease(T x, T y, T linear_thresh = T(0.01)) {
    if (abs(y) < linear_thresh) {
        return x;
    }
    return (std::exp(x * y) - T(1.0)) / (std::exp(y) - T(1.0));
}

template <std::floating_point T>
inline T db_to_linear(T x, T threshold = -72.0f) {
    if (x <= threshold) {
        return 0.0f;
    }
    return std::pow(T(10), T((double)x * 0.05));
}

template <std::floating_point T>
inline T linear_to_db(T x) {
    return T(20) * std::log10(std::abs(x));
}

template <std::floating_point T>
inline T lerp(T x, T a, T b) {
    return (1.0 - x) * a + x * b;
}

template <std::floating_point T>
inline T normalize_value(T value, T min_val, T max_val) {
    return (min_val - value) / (min_val - max_val);
}

template <std::floating_point T>
inline T unnormalize_value(T value, T min_val, T max_val) {
    return value * (max_val - min_val) + min_val;
}

template <std::floating_point T>
inline bool near_equal(T a, T b, T eps = small_value<T>) {
    return abs(a - b) < eps;
}

template <std::floating_point T>
inline bool near_equal_to_zero(T value, T eps = small_value<T>) {
    return abs(value) < eps;
}

} // namespace math

template <typename T, typename V>
concept NormalizedRange = std::floating_point<V> && requires(T a, V v) {
    { a.plain_to_normalized(v) } -> std::floating_point;
    { a.normalized_to_plain(v) } -> std::floating_point;
};

struct LinearRange {
    float min_val;
    float max_val;

    inline float plain_to_normalized(float plain) const { return math::normalize_value(plain, min_val, max_val); }
    inline float normalized_to_plain(float normalized) const { return math::unnormalize_value(normalized, min_val, max_val); }
};

struct NonLinearRange {
    float min_val;
    float max_val;
    float range;
    float power;
    float exp_norm;

    NonLinearRange(float min, float max, float power) : power(power) {
        min_val = min;
        max_val = max;
        range = max - min;
        exp_norm = (float)(std::exp((double)power) - 1.0);
    }

    inline float plain_to_normalized(float plain) const {
        float input = math::clamp(plain, min_val, max_val);
        float normalized = std::log((plain - min_val) / range * exp_norm + 1.0) / power;
        return normalized;
    }

    inline float normalized_to_plain(float normalized) const {
        float input = math::clamp(normalized, 0.0f, 1.0f);
        float v = (std::exp(normalized * power) - 1.0f) / exp_norm;
        return v * range + min_val;
    }
};

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

inline static double samples_to_beat(size_t samples, double sample_rate, double beat_duration) {
    double sec = (double)samples / sample_rate;
    return sec / beat_duration;
}

inline static double samples_to_beat(double samples, double sample_rate, double beat_duration) {
    double sec = samples / sample_rate;
    return sec / beat_duration;
}

inline static double beat_to_samples(double beat, double sample_rate, double beat_duration) {
    double sec = beat * beat_duration;
    return (sec * sample_rate);
}

} // namespace wb