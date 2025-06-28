#pragma once

#include "core/common.h"
#include "sample.h"

namespace wb::dsp {

enum class ResamplerType {
  Nearest,
  Linear,
};

struct Sampler {
  double playback_speed_;
  double sample_offset_;
  ResamplerType resampler_type_;

  void reset_state(
      ResamplerType resampler_type,
      double sample_offset,
      double speed,
      double src_sample_rate,
      double dst_sample_rate) {
    playback_speed_ = (src_sample_rate / dst_sample_rate) * speed;
    sample_offset_ = sample_offset;
    resampler_type_ = resampler_type;
  }

  void stream(
      Sample* sample,
      uint32_t num_channels,
      uint32_t num_samples,
      uint32_t buffer_offset,
      float gain,
      float** dst_out_buffer);
};

}  // namespace wb::dsp