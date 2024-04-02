#pragma once

#include "common.h"
#include <cmath>

namespace wb {

inline static double samples_to_beat(size_t samples, double sample_rate, double beat_duration) {
    double sec = (double)samples / sample_rate;
    return sec / beat_duration;
}

inline static double beat_to_samples(double beat, double sample_rate, double beat_duration) {
    double sec = beat * beat_duration;
    return std::round(sec * sample_rate);
}

} // namespace wb