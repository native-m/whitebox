#pragma once

#include "common.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace wb {

struct Spinlock {
    std::atomic_bool lock_;

    inline bool try_lock() noexcept {
        return !lock_.load(std::memory_order_relaxed) && !lock_.exchange(true, std::memory_order_acquire);
    }

    inline void lock() noexcept {
        for (;;) {
            if (!lock_.exchange(true, std::memory_order_acquire))
                return;
            while (lock_.load(std::memory_order_relaxed))
                std::this_thread::yield();
        }
    }

    inline void unlock() noexcept { lock_.store(false, std::memory_order_release); }

    inline void wait() noexcept {
        while (lock_.load(std::memory_order_relaxed))
            std::this_thread::yield();
    }
};

void accurate_sleep_ns(int64_t timeout_ns);

template <typename T, typename Period>
inline void accurate_sleep(const std::chrono::duration<T, Period>& duration) {
    if constexpr (std::same_as<T, std::chrono::nanoseconds::rep>) {
        accurate_sleep_ns(duration.count());
    } else {
        auto nano = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        accurate_sleep_ns(nano.count());
    }
}

} // namespace wb
