#pragma once

#include "../core/debug.h"
#include "../stdpch.h"

namespace wb
{
    struct Spinlock
    {
        std::atomic_bool lock_;

        bool try_lock() noexcept
        {
            return !lock_.load(std::memory_order_relaxed) &&
                !lock_.exchange(true, std::memory_order_acquire);
        }

        void lock() noexcept
        {
            for (;;) {
                if (!lock_.exchange(true, std::memory_order_acquire))
                    return;
                while (lock_.load(std::memory_order_relaxed));
            }
        }

        void unlock() noexcept
        {
            lock_.store(false, std::memory_order_release);
        }
    };

    inline void engage_unique_lock(std::shared_mutex& mutex)
    {

    }

    template<typename T, typename Period>
    inline void sleep_for(const std::chrono::duration<T, Period>& time)
    {

    }

    template<typename T, typename Period>
    void accurate_sleep_for(const std::chrono::duration<T, Period>& time)
    {
        constexpr auto milli = std::chrono::milliseconds(1);
        constexpr auto sec_ratio = 1.0 / 1e9;
        auto current_time = std::chrono::duration<double>(time).count();
        static auto estimate = 5e-3;
        static auto mean = 5e-3;
        static double m2 = 0;
        static int64_t count = 1;

        while (current_time > estimate) {
            auto start = std::chrono::high_resolution_clock::now();
            std::this_thread::sleep_for(milli);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto observed = (end - start).count() * sec_ratio;
            current_time -= observed;

            ++count;
            auto delta = observed - mean;
            mean += delta / count;
            m2 += delta * delta;
            auto stddev = std::sqrt(m2 / (count - 1));
            estimate = mean + stddev;
        }

        auto start = std::chrono::high_resolution_clock::now();
        while ((std::chrono::high_resolution_clock::now() - start).count() * sec_ratio < current_time);
        //Log::info("{}", count);
    }
}