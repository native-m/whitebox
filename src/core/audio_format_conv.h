#pragma once

#include "common.h"
#include "math.h"
#include <limits>

namespace wb {

inline void convert_f32_to_interleaved_i16(int16_t* dst, const float* const* src, size_t src_offset,
                                           size_t num_samples, uint32_t num_channels) {
    static constexpr float min_val = (float)-std::numeric_limits<int16_t>::min();
    static constexpr float max_val = (float)std::numeric_limits<int16_t>::max();
    for (uint32_t c = 0; c < num_channels; c++) {
        const float* src_channel = src[c] + src_offset;
        for (size_t i = 0; i < num_samples; i++) {
            float src_sample = src_channel[i];
            dst[i * num_channels + c] =
                (int16_t)(src_sample > 0.0f ? src_sample * max_val : src_sample * min_val);
        }
    }
}

inline void convert_f32_to_interleaved_i24(std::byte* dst, const float* const* src,
                                           size_t src_offset, size_t num_samples,
                                           uint32_t num_channels) {
    static constexpr float min_val = 8388608.0f;
    static constexpr float max_val = 8388607.0f;
    for (uint32_t c = 0; c < num_channels; c++) {
        const float* src_channel = src[c] + src_offset;
        size_t num_bytes = num_samples * 3;
        size_t j = 0;
        for (size_t i = 0; i < num_bytes; i += 3) {
            float src_sample = src_channel[j];
            int32_t converted = src_sample > 0.0f ? (int32_t)(src_sample * max_val)
                                                  : (int32_t)(src_sample * min_val);
            dst[i + 0] = std::byte(converted);
            dst[i + 1] = std::byte(converted >> 8u);
            dst[i + 2] = std::byte(converted >> 16u);
            j++;
        }
    }
}

inline void convert_f32_to_interleaved_i24_x8(int32_t* dst, const float* const* src,
                                              size_t src_offset, size_t num_samples,
                                              uint32_t num_channels) {
    static constexpr float min_val = 8388608.0f;
    static constexpr float max_val = 8388607.0f;
    for (uint32_t c = 0; c < num_channels; c++) {
        const float* src_channel = src[c] + src_offset;
        for (size_t i = 0; i < num_samples; i++) {
            float src_sample = src_channel[i];
            int32_t converted = src_sample > 0.0f ? (int32_t)(src_sample * max_val)
                                                  : (int32_t)(src_sample * min_val);
            dst[i * num_channels + c] = (int32_t)(converted & 0xFFFFFF);
        }
    }
}

inline void convert_f32_to_interleaved_i32(int32_t* dst, const float* const* src, size_t src_offset,
                                           size_t num_samples, uint32_t num_channels) {
    static constexpr double min_val = (double)-std::numeric_limits<int32_t>::min();
    static constexpr double max_val = (double)std::numeric_limits<int32_t>::max();
    for (uint32_t c = 0; c < num_channels; c++) {
        const float* src_channel = src[c] + src_offset;
        for (size_t i = 0; i < num_samples; i++) {
            float src_sample = src_channel[i];
            dst[i * num_channels + c] = (int32_t)(src_sample > 0.0f ? (double)src_sample * max_val
                                                                    : (double)src_sample * min_val);
        }
    }
}

inline void convert_to_interleaved_f32(float* dst, const float* const* src, size_t src_offset,
                                       size_t num_samples, uint32_t num_channels) {
    for (uint32_t c = 0; c < num_channels; c++) {
        const float* src_channel = src[c] + src_offset;
        for (size_t i = 0; i < num_samples; i++) {
            dst[i * num_channels + c] = src_channel[i];
        }
    }
}

} // namespace wb