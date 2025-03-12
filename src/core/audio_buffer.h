#pragma once

#include "audio_format.h"
#include "audio_format_conv.h"
#include "memory.h"
#include "types.h"

#define WB_AUDIO_BUFFER_ALIGNEMNT 64

namespace wb {

// Buffer for storing audio samples.
// Only supports floating-point samples, but this might change in the future.
template<std::floating_point T>
struct AudioBuffer {
  static constexpr uint32_t internal_buffer_capacity = 16;
  static constexpr uint32_t alignment = 32;

  uint32_t n_samples{};
  uint32_t n_channels{};
  uint32_t channel_capacity = internal_buffer_capacity;
  T* internal_channel_buffers[internal_buffer_capacity]{};
  T** channel_buffers{};

  inline AudioBuffer() : channel_buffers(internal_channel_buffers) {
  }

  inline AudioBuffer(uint32_t sample_count, uint32_t channel_count)
      : n_samples(sample_count),
        n_channels(channel_count),
        channel_buffers(internal_channel_buffers) {
    resize_channel_array_(n_channels);
    for (uint32_t i = 0; i < n_channels; i++) {
      channel_buffers[i] = (T*)allocate_aligned(sample_count * sizeof(T), alignment);
      assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
      std::memset(channel_buffers[i], 0, sample_count * sizeof(T));
    }
  }

  inline ~AudioBuffer() {
    for (uint32_t i = 0; i < n_channels; i++) {
      free_aligned(channel_buffers[i]);
      channel_buffers[i] = nullptr;
    }
    if (channel_buffers != internal_channel_buffers)
      std::free(channel_buffers);
  }

  inline T* get_write_pointer(uint32_t channel, uint32_t sample_offset = 0) {
    assert(channel < n_channels && "Channel out of range");
    return channel_buffers[channel] + sample_offset;
  }

  inline const T* get_read_pointer(uint32_t channel, uint32_t sample_offset = 0) const {
    assert(channel < n_channels && "Channel out of range");
    return channel_buffers[channel] + sample_offset;
  }

  inline void set_sample(uint32_t channel, uint32_t sample_offset, T sample) const {
    channel_buffers[channel][sample_offset] = sample;
  }

  inline void mix_sample(uint32_t channel, uint32_t sample_offset, T sample) const {
    channel_buffers[channel][sample_offset] += sample;
  }

  inline void clear() {
    for (uint32_t i = 0; i < n_channels; i++) {
      std::memset(channel_buffers[i], 0, n_samples * sizeof(T));
    }
  }

  inline void mix(const AudioBuffer<T>& other) {
    assert(n_samples == other.n_samples);
    for (uint32_t i = 0; i < n_channels; i++) {
      const float* other_buffer = other.channel_buffers[i];
      float* buffer = channel_buffers[i];
      for (uint32_t j = 0; j < n_samples; j++) {
        buffer[j] += other_buffer[j];
      }
    }
  }

  inline void resize(uint32_t samples, bool clear = false) {
    if (samples == n_samples)
      return;

    size_t new_size = samples * sizeof(T);
    if (clear) {
      for (uint32_t i = 0; i < n_channels; i++) {
        free_aligned(channel_buffers[i]);
        channel_buffers[i] = (T*)allocate_aligned(new_size, alignment);
        assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
        std::memset(channel_buffers[i], 0, new_size);
      }
    } else {
      for (uint32_t i = 0; i < n_channels; i++) {
        T* old_buffer = channel_buffers[i];
        channel_buffers[i] = (T*)allocate_aligned(new_size, alignment);
        assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
        size_t count = std::min(n_samples, samples);
        std::memcpy(channel_buffers[i], old_buffer, count * sizeof(T));
        if (samples > n_samples)
          std::memset(channel_buffers[i] + n_samples, 0, (samples - n_samples) * sizeof(T));
        free_aligned(old_buffer);
      }
    }

    n_samples = samples;
  }

  inline void resize_channel(uint32_t channel_count) {
    assert(n_samples != 0);
    if (channel_count == n_channels)
      return;
    uint32_t old_channel_count = n_channels;
    resize_channel_array_(channel_count);
    if (channel_count > old_channel_count) {
      // Allocate new buffer for new channels
      for (uint32_t i = old_channel_count; i < channel_count; i++) {
        channel_buffers[i] = (T*)allocate_aligned(n_samples * sizeof(T), alignment);
        assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
        std::memset(channel_buffers[i], 0, n_samples * sizeof(T));
      }
    } else {
      uint32_t shrink_size = old_channel_count - channel_count;
      for (uint32_t i = channel_count; i < old_channel_count; i++) {
        free_aligned(channel_buffers[i]);
        channel_buffers[i] = nullptr;
      }
    }
  }

  inline void deinterleave_samples_from(const void* src, uint32_t dst_offset, uint32_t count, AudioFormat format) {
    switch (format) {
      case AudioFormat::F32:
        convert_to_deinterleaved_f32(channel_buffers, (float*)src, dst_offset, count, n_channels);
        break;
      default: assert(false);
    }
  }

  inline void interleave_samples_to(void* dst, uint32_t offset, uint32_t count, AudioFormat format) {
    switch (format) {
      case AudioFormat::I16:
        convert_f32_to_interleaved_i16((int16_t*)dst, channel_buffers, offset, count, n_channels);
        break;
      case AudioFormat::I24:
        convert_f32_to_interleaved_i24((std::byte*)dst, channel_buffers, offset, count, n_channels);
        break;
      case AudioFormat::I24_X8:
        convert_f32_to_interleaved_i24_x8((int32_t*)dst, channel_buffers, offset, count, n_channels);
        break;
      case AudioFormat::I32:
        convert_f32_to_interleaved_i32((int32_t*)dst, channel_buffers, offset, count, n_channels);
        break;
      case AudioFormat::F32: convert_to_interleaved_f32((float*)dst, channel_buffers, offset, count, n_channels); break;
      default: assert(false);
    }
  }

  inline void resize_channel_array_(uint32_t channel_count) {
    if (channel_count < channel_capacity) {
      n_channels = channel_count;
      return;
    }

    T** new_channel_buffers = (T**)std::malloc(channel_count * sizeof(T*));
    assert(new_channel_buffers && "Cannot allocate memory for audio channel array");
    std::memcpy(new_channel_buffers, channel_buffers, n_channels * sizeof(T*));
    std::free(channel_buffers);
    channel_buffers = new_channel_buffers;
    n_channels = channel_count;
  }
};

}  // namespace wb