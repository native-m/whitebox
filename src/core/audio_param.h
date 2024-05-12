#pragma once

#include "types.h"
#include "vector.h"
#include <atomic>

namespace wb {

struct AudioParameterData {
    mutable std::atomic_bool updated;
    union {
        std::atomic<int32_t> i32;
        std::atomic<uint32_t> u32;
        std::atomic<float> f32;
    };

    AudioParameterData() : i32() {}
};

struct AudioParameterList {
    std::atomic_bool params_updated;
    Vector<AudioParameterData> params;

    inline void resize(uint32_t param_count) { params.resize(param_count); }

    inline void update() { params_updated.store(true, std::memory_order_release); }

    inline bool is_updated(uint32_t id) const {
        return params[id].updated.load(std::memory_order_relaxed);
    }
    
    inline void set(uint32_t id, int32_t value) {
        params[id].updated.store(true, std::memory_order_relaxed);
        params[id].i32.store(value, std::memory_order_relaxed);
    }

    inline void set(uint32_t id, uint32_t value) {
        params[id].updated.store(true, std::memory_order_relaxed);
        params[id].u32.store(value, std::memory_order_relaxed);
    }

    inline void set(uint32_t id, float value) {
        params[id].updated.store(true, std::memory_order_relaxed);
        params[id].f32.store(value, std::memory_order_relaxed);
    }

    inline int32_t get_int(uint32_t id) const {
        return params[id].i32.load(std::memory_order_relaxed);
    }

    inline uint32_t get_uint(uint32_t id) const {
        return params[id].u32.load(std::memory_order_relaxed);
    }

    inline float get_float(uint32_t id) const {
        return params[id].f32.load(std::memory_order_relaxed);
    }

    inline int32_t flush_int(uint32_t id) const {
        params[id].updated.store(false, std::memory_order_relaxed);
        return params[id].i32.load(std::memory_order_relaxed);
    }
    
    inline uint32_t flush_uint(uint32_t id) const {
        params[id].updated.store(false, std::memory_order_relaxed);
        return params[id].u32.load(std::memory_order_relaxed);
    }

    inline float flush_float(uint32_t id) const {
        params[id].updated.store(false, std::memory_order_relaxed);
        return params[id].f32.load(std::memory_order_relaxed);
    }

    template <typename Fn>
    void flush_if_updated(Fn&& callback)
        requires std::invocable<Fn, const AudioParameterList&>
    {
        if (params_updated.load(std::memory_order_acquire)) {
            callback(*this);
            params_updated.store(false, std::memory_order_release);
        }
    }
};

} // namespace wb