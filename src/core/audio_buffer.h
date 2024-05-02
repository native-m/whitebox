#pragma once

#include "memory.h"
#include "types.h"

#define WB_AUDIO_BUFFER_ALIGNEMNT 64

namespace wb {

// Buffer for storing audio samples.
// Only supports floating-point samples, but this might change in the future.
template <std::floating_point T>
struct AudioBuffer {
    static constexpr uint32_t internal_buffer_capacity = 16;

    uint32_t n_samples {};
    uint32_t n_channels {};
    uint32_t channel_capacity = internal_buffer_capacity;
    T* internal_channel_buffers[internal_buffer_capacity] {};
    T** channel_buffers {};
    bool managed;

    AudioBuffer(uint32_t sample_count, uint32_t channel_count) :
        n_samples(sample_count),
        n_channels(channel_count),
        channel_buffers(internal_channel_buffers) {
        resize_channel_array_(n_channels);
        for (uint32_t i = 0; i < n_channels; i++) {
            channel_buffers[i] =
                allocate_aligned(sample_count * sizeof(T), WB_AUDIO_BUFFER_ALIGNEMNT);
            assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
            std::memset(channel_buffers[i], 0, sample_count * sizeof(T));
        }
    }

    ~AudioBuffer() {
        for (uint32_t i = 0; i < n_channels; i++) {
            free_aligned(channel_buffers[i]);
            channel_buffers[i] = nullptr;
        }
        if (channel_buffers != internal_buffer_capacity)
            std::free(channel_buffers);
    }

    T* get_write_pointer(uint32_t channel) {
        assert(channel < n_channels && "Channel out of range");
        return channel_buffers[channel];
    }

    const T* get_read_pointer(uint32_t channel) const {
        assert(channel < n_channels && "Channel out of range");
        return channel_buffers[channel];
    }

    void resize(uint32_t samples, bool clear = false) {
        if (samples == n_samples)
            return;

        size_t new_size = samples * sizeof(T);
        if (clear) {
            for (uint32_t i = 0; i < n_channels; i++) {
                free_aligned(channel_buffers[i]);
                channel_buffers[i] = allocate_aligned(new_size, WB_AUDIO_BUFFER_ALIGNEMNT);
                assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
                std::memset(channel_buffers[i], 0, new_size);
            }
        } else {
            for (uint32_t i = 0; i < n_channels; i++) {
                T* old_buffer = channel_buffers[i];
                channel_buffers[i] = allocate_aligned(new_size, WB_AUDIO_BUFFER_ALIGNEMNT);
                assert(channel_buffers[i] && "Cannot allocate memory for audio buffer");
                std::memcpy(channel_buffers[i], old_buffer, n_samples * sizeof(T));
                std::memset(channel_buffers[i] + n_samples, 0, (samples - n_samples) * sizeof(T));
                free_aligned(old_buffer);
            }
        }

        n_samples = samples;
    }

    void resize_channel_array_(uint32_t channel_count) {
        if (channel_count > channel_capacity) {
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

} // namespace wb