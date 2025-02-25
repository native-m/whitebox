#pragma once

#include "common.h"
#include "core_math.h"
#include <atomic>

namespace wb {
uint64_t tm_get_ticks();
uint64_t tm_get_ticks_per_seconds();

inline static double tm_ticks_to_sec(uint64_t ticks) {
    return (double)ticks / (double)tm_get_ticks_per_seconds();
}

inline static double tm_ticks_to_ms(uint64_t ticks) {
    return ((double)ticks * 1'000.0) / (double)tm_get_ticks_per_seconds();
}

inline static double tm_ticks_to_us(uint64_t ticks) {
    return ((double)ticks * 1'000'000.0) / (double)tm_get_ticks_per_seconds();
}

inline static double tm_ticks_to_ns(uint64_t ticks) {
    return ((double)ticks * 1'000'000'000.0) / (double)tm_get_ticks_per_seconds();
}

inline static constexpr double tm_ms_to_sec(double ms) {
    return ms / 1000.0;
}

inline static constexpr double tm_sec_to_ms(double ms) {
    return ms * 1000.0;
}

struct ScopedPerformanceCounter {
    uint64_t start_ticks;
    inline ScopedPerformanceCounter() : start_ticks(tm_get_ticks()) {}
    inline uint64_t duration() const { return tm_get_ticks() - start_ticks; }
};

struct PerformanceMeasurer {
    std::atomic<double> usage;

    void update(double duration, double target_duration) {
        double percentage = duration / target_duration;
        double old_usage = usage.load(std::memory_order_relaxed);
        double new_usage = old_usage + 0.25 * (percentage - old_usage);
        usage.store(new_usage, std::memory_order_release);
    }

    double get_usage() const { return math::clamp(usage.load(std::memory_order_acquire), 0.0, 1.0); }
};

} // namespace wb