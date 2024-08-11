#pragma once

#include "core/audio_buffer.h"
#include "core/debug.h"
#include "core/math.h"
#include <atomic>

namespace wb {
enum class LevelMeterColorMode {
    Normal,
    Line,
};

struct VUMeter {
    std::atomic<float> level;
    float current_level = 0.0f;

    void push_samples(const AudioBuffer<float>& buffer, uint32_t channel) {
        const float* samples = buffer.get_read_pointer(channel);
        float new_level = 0.0f;
        for (uint32_t i = 0; i < buffer.n_samples; i++) {
            new_level = math::max(new_level, math::abs(samples[i]));
        }
        float old_level = level.load(std::memory_order_relaxed);
        while (old_level < new_level &&
               level.compare_exchange_weak(old_level, new_level, std::memory_order_release,
                                           std::memory_order_relaxed))
            ;
    }

    void update(float frame_rate) {
        float new_level = level.exchange(0.0f, std::memory_order_release);
        if (new_level > current_level) {
            current_level = new_level;
        } else {
            float update_rate = 1.0f - std::exp(-1.0f / (frame_rate * 0.1f));
            current_level += (new_level - current_level) * update_rate;
        }
    }

    inline float get_value() const { return current_level; }
};
} // namespace wb