#pragma once

#include <atomic>
#include "core/common.h"

namespace wb {

struct Spinlock {
    std::atomic_bool lock_;

    bool try_lock() noexcept {
        return !lock_.load(std::memory_order_relaxed) &&
               !lock_.exchange(true, std::memory_order_acquire);
    }

    void lock() noexcept {
        for (;;) {
            if (!lock_.exchange(true, std::memory_order_acquire))
                return;
            while (lock_.load(std::memory_order_relaxed))
                ;
        }
    }

    void unlock() noexcept { lock_.store(false, std::memory_order_release); }
};

} // namespace wb
