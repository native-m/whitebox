#pragma once

#include "common.h"
#include "math.h"
#include <limits>

namespace wb {

void convert_f32_to_interleaved_i16(int16_t* dst, const float* const* src, size_t src_offset, size_t num_samples,
                                    uint32_t num_channels);

void convert_f32_to_interleaved_i24(std::byte* dst, const float* const* src, size_t src_offset, size_t num_samples,
                                    uint32_t num_channels);

void convert_f32_to_interleaved_i24_x8(int32_t* dst, const float* const* src, size_t src_offset, size_t num_samples,
                                       uint32_t num_channels);

void convert_f32_to_interleaved_i32(int32_t* dst, const float* const* src, size_t src_offset, size_t num_samples,
                                    uint32_t num_channels);

void convert_to_interleaved_f32(float* dst, const float* const* src, size_t src_offset, size_t num_samples,
                                uint32_t num_channels);

void convert_to_deinterleaved_f32(float* const* dst, const float* src, size_t dst_offset, size_t num_samples,
                                  uint32_t num_channels);

} // namespace wb