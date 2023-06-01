#pragma once

#include "memory.h"
#include "audio_format.h"
#include "debug.h"
#include <cstddef>
#include <optional>
#include <vector>
#include <ranges>

namespace wb
{
    template<typename T>
        requires std::floating_point<T>
    struct AudioBuffer
    {
        AudioFormat format{};
        uint32_t n_samples{};
        uint32_t n_channels{};
        std::vector<T*> buffers_;
        bool managed_{};

        AudioBuffer(AudioFormat format, uint32_t n_samples, uint32_t n_channels) :
            format(format),
            n_samples(n_samples),
            n_channels(n_channels),
            managed_(true)
        {
            WB_ASSERT(is_floating_point_format(format));
            // Allocate buffer for every channel
            uint32_t sample_size = get_audio_sample_size(format);
            buffers_.reserve(n_channels);
            for (uint32_t i = 0; i < n_channels; i++) {
                // Use aligned buffer
                T* buffer = (T*)allocate_aligned(n_samples * sample_size, 64);
                std::memset(buffer, 0, n_samples * sample_size);
                WB_CHECK(buffer != nullptr && "Out of memory");
                buffers_.push_back(buffer);
            }
        }

        AudioBuffer(T* const* buffer_mem, AudioFormat format, uint32_t n_samples, uint32_t n_channels) :
            format(format),
            n_samples(n_samples),
            n_channels(n_channels),
            managed_(false)
        {
            WB_ASSERT(is_floating_point_format(format));
            buffers_.resize(n_channels);
            for (uint32_t i = 0; i < n_samples; i++)
                buffers_[i] = buffer_mem[i];
        }

        ~AudioBuffer()
        {
            if (managed_)
                for (auto buffer : buffers_)
                    free_aligned(buffer);
        }

        inline const T* get_read_pointer(uint32_t channel) const
        {
            return (const T*)buffers_[channel];
        }

        inline T* get_write_pointer(uint32_t channel)
        {
            return (T*)buffers_[channel];
        }
    };
}