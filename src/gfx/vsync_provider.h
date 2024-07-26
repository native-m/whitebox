#pragma once

#include "core/common.h"
#include "core/thread.h"
#include <chrono>
#include <memory>

namespace wb {

// A fallback timer-based vsync provider. Not accurate but enough for us.
struct VsyncProvider {
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;

    VsyncProvider() { start_time_ = std::chrono::high_resolution_clock::now(); }

    virtual void wait_for_vblank() {
        auto frame_time = std::chrono::high_resolution_clock::now() - start_time_;
        static constexpr auto target_rate =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(
                1.0 / 61.0)); // Slightly deviated to compensate inaccuracy
        if (frame_time < target_rate) {
            auto wait_time = target_rate - frame_time;
            accurate_sleep(wait_time);
        }
        start_time_ = std::chrono::high_resolution_clock::now();
    }
};

extern std::unique_ptr<VsyncProvider> g_vsync_provider;
} // namespace wb