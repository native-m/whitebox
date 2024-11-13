#pragma once

#include "common.h"
//#include "intrinsics.h"
#include "types.h"

namespace wb::dsp {
template <std::floating_point T>
static T find_abs_maximum(const T* arr, uint32_t count) {
    T abs_max = T(0);
    for (uint32_t i = 0; i < count; i++) {
        T v = arr[i];
        T abs_v = v < T(0) ? -v : v;
        abs_max = abs_v < abs_max ? abs_max : abs_v;
    }
    return abs_max;
}

template <std::floating_point T>
static void gain(const T* input, T* output, uint32_t count, float gain) {
    for (uint32_t i = 0; i < count; i++)
        input[i] = output[i] * gain;
}

template <std::floating_point T>
static void apply_gain(T* inout, uint32_t count, float gain) {
    for (uint32_t i = 0; i < count; i++)
        inout[i] *= gain;
}

} // namespace wb