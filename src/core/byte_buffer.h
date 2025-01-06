#pragma once

#include "core/common.h"
#include "core/vector.h"
#include <cstddef>
#include <string>

namespace wb {
struct ByteBuffer {
    Vector<std::byte> buffer;
    size_t write_pos = 0;
    size_t read_pos = 0;

    ByteBuffer(const ByteBuffer&) = delete;

    inline void reset() { write_pos = 0; }
    inline void reserve(size_t n) { buffer.resize(n); }

    inline uint32_t write(const void* src, size_t size) {
        std::byte* ptr = buffer.data() + write_pos;
        std::memcpy(ptr, src, size);
        write_pos += size;
        return (uint32_t)size;
    }

    // TODO: Implement read functions

    inline uint32_t write_i32(int32_t value) { return write(&value, sizeof(int32_t)); }
    inline uint32_t write_u32(uint32_t value) { return write(&value, sizeof(uint32_t)); }
    inline uint32_t write_f32(float value) { return write(&value, sizeof(float)); }
    inline uint32_t write_i64(int32_t value) { return write(&value, sizeof(int64_t)); }
    inline uint32_t write_u64(uint32_t value) { return write(&value, sizeof(uint64_t)); }
    inline uint32_t write_f64(double value) { return write(&value, sizeof(double)); }
    inline uint32_t write_string(const char* str, size_t size) { return write(str, size); }
    inline size_t write_size() const { return write_pos; }
    inline const std::byte* data() const { return buffer.data(); }
};
} // namespace wb