#include "sampler.h"

#include "core/core_math.h"

namespace wb::dsp {

template<AudioFormat Fmt>
inline static constexpr auto get_pcm_sample_normalizer() {
  if constexpr (Fmt == AudioFormat::I16) {
    return (float)(1.0 / static_cast<double>(INT16_MAX));
  } else if constexpr (Fmt == AudioFormat::I24) {
    return (double)(1.0 / static_cast<double>((1 << 23) - 1));
  } else if constexpr (Fmt == AudioFormat::I32) {
    return (double)(1.0 / static_cast<double>(INT32_MAX));
  } else if constexpr (Fmt == AudioFormat::F32) {
    return 1.0f;
  }
}

static void sample_nearest(
    Sampler* sampler,
    Sample* sample,
    uint32_t channel,
    uint32_t num_samples,
    size_t sample_offset,
    float* output_buffer) {
  switch (sample->format) {
    case AudioFormat::I16: break;
    case AudioFormat::I24: break;
    default: break;
  }
}

template<typename T, AudioFormat Fmt>
inline static void sample_linear(
    uint32_t num_channels,
    uint32_t num_samples,
    uint32_t buffer_offset,
    float gain,
    double playback_speed,
    double sample_position,
    const T* const* src_channels,
    float** output_buffer) {
  static constexpr auto pcm_normalizer = get_pcm_sample_normalizer<Fmt>();
  using NormalizerT = decltype(pcm_normalizer);
  for (int32_t i = 0; i < num_channels; i++) {
    const T* src_sample = src_channels[i];
    float* dst_buffer = output_buffer[i] + buffer_offset;
    for (int32_t j = 0; j < num_samples; j++) {
      const double x = sample_position + ((double)j * playback_speed);
      const int64_t ix = (int64_t)x;
      const float fx = (float)(x - (double)ix);
      const float a = (float)(pcm_normalizer * (NormalizerT)src_sample[ix]);
      const float b = (float)(pcm_normalizer * (NormalizerT)src_sample[ix + 1]);
      const float s = a + fx * (b - a);
      dst_buffer[j] += s * gain;
    }
  }
}

template<typename T, AudioFormat Fmt>
inline static void sample_catmull_rom(
    uint32_t num_channels,
    uint32_t num_samples,
    uint32_t buffer_offset,
    float gain,
    double playback_speed,
    double sample_position,
    const T* const* src_channels,
    float** output_buffer) {
  static constexpr auto pcm_normalizer = get_pcm_sample_normalizer<Fmt>();
  using NormalizerT = decltype(pcm_normalizer);
  for (uint32_t i = 0; i < num_channels; i++) {
    const T* src_sample = src_channels[i];
    float* dst_buffer = output_buffer[i] + buffer_offset;
    for (uint32_t j = 0; j < num_samples; j++) {
      const double x = (sample_position + (double)j) * playback_speed;
      const int64_t ix = (int64_t)x;
      const float fx = (float)(x - (double)ix);
      const float a = (float)(pcm_normalizer * (NormalizerT)src_sample[ix - 1]);
      const float b = (float)(pcm_normalizer * (NormalizerT)src_sample[ix]);
      const float c = (float)(pcm_normalizer * (NormalizerT)src_sample[ix + 1]);
      const float d = (float)(pcm_normalizer * (NormalizerT)src_sample[ix + 2]);
    }
  }
}

void Sampler::stream(
    Sample* sample,
    uint32_t num_channels,
    uint32_t num_samples,
    uint32_t buffer_offset,
    float gain,
    float** dst_out_buffer) {
  static constexpr float i16_pcm_normalizer = 1.0f / static_cast<float>(std::numeric_limits<int16_t>::max());
  static constexpr double i24_pcm_normalizer = 1.0 / static_cast<double>((1 << 23) - 1);
  static constexpr double i32_pcm_normalizer = 1.0 / static_cast<double>(std::numeric_limits<int32_t>::max());

  if (sample_offset_ >= sample->count)
    return;  // has finished streaming

  double stream_max_length = ((double)sample->count - sample_offset_) / playback_speed_;
  double next_sample_offset = sample_offset_ + ((double)num_samples * playback_speed_);
  uint32_t num_actual_samples = std::min(num_samples, (uint32_t)std::ceil(stream_max_length));

  if (playback_speed_ == 1.0) {
    uint32_t sample_offset_u32 = (uint32_t)sample_offset_;
    switch (sample->format) {
      case AudioFormat::I16: {
        for (uint32_t i = 0; i < num_channels; i++) {
          int32_t c = i % sample->channels;
          const int16_t* sample_data = sample->get_read_pointer<int16_t>(c);
          float* output_buffer = dst_out_buffer[i] + buffer_offset;
          for (uint32_t j = 0; j < num_actual_samples; j++) {
            float sample = (float)sample_data[sample_offset_u32 + j] * i16_pcm_normalizer;
            output_buffer[j] += math::clamp(sample, -1.0f, 1.0f) * gain;
          }
        }
        break;
      }
      case AudioFormat::I24: {
        for (uint32_t i = 0; i < num_channels; i++) {
          int32_t c = i % sample->channels;
          const int32_t* sample_data = sample->get_read_pointer<int32_t>(c);
          float* output_buffer = dst_out_buffer[i] + buffer_offset;
          for (uint32_t j = 0; j < num_actual_samples; j++) {
            double sample = (double)sample_data[sample_offset_u32 + j] * i24_pcm_normalizer;
            output_buffer[j] += (float)math::clamp(sample, -1.0, 1.0) * gain;
          }
        }
        break;
      }
      case AudioFormat::I32: {
        for (uint32_t i = 0; i < num_channels; i++) {
          int32_t c = i % sample->channels;
          const int32_t* sample_data = sample->get_read_pointer<int32_t>(c);
          float* output_buffer = dst_out_buffer[i] + buffer_offset;
          for (uint32_t j = 0; j < num_actual_samples; j++) {
            double sample = (double)sample_data[sample_offset_u32 + j] * i32_pcm_normalizer;
            output_buffer[j] += (float)math::clamp(sample, -1.0, 1.0) * gain;
          }
        }
        break;
      }
      case AudioFormat::F32: {
        for (uint32_t i = 0; i < num_channels; i++) {
          int32_t c = i % sample->channels;
          const float* sample_data = sample->get_read_pointer<float>(c);
          float* output_buffer = dst_out_buffer[i] + buffer_offset;
          for (uint32_t j = 0; j < num_actual_samples; j++) {
            float sample = sample_data[sample_offset_u32 + j];
            output_buffer[j] += sample * gain;
          }
        }
        break;
      }
      default: WB_UNREACHABLE();
    }
  } else {
    switch (sample->format) {
      case AudioFormat::I16:
        sample_linear<int16_t, AudioFormat::I16>(
            num_channels,
            num_actual_samples,
            buffer_offset,
            gain,
            playback_speed_,
            sample_offset_,
            sample->get_sample_data<int16_t>(),
            dst_out_buffer);
        break;
      case AudioFormat::I24:
        sample_linear<int32_t, AudioFormat::I24>(
            num_channels,
            num_actual_samples,
            buffer_offset,
            gain,
            playback_speed_,
            sample_offset_,
            sample->get_sample_data<int32_t>(),
            dst_out_buffer);
        break;
      case AudioFormat::I32:
        sample_linear<int32_t, AudioFormat::I32>(
            num_channels,
            num_actual_samples,
            buffer_offset,
            gain,
            playback_speed_,
            sample_offset_,
            sample->get_sample_data<int32_t>(),
            dst_out_buffer);
        break;
      case AudioFormat::F32:
        sample_linear<float, AudioFormat::F32>(
            num_channels,
            num_actual_samples,
            buffer_offset,
            gain,
            playback_speed_,
            sample_offset_,
            sample->get_sample_data<float>(),
            dst_out_buffer);
        break;
      default: WB_UNREACHABLE();
    }
  }

  sample_offset_ = next_sample_offset;
}

}  // namespace wb::dsp