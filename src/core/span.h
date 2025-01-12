#pragma once

#include "core/common.h"

namespace wb {
template <typename T>
struct Span {
    T* data;
    size_t size;

    inline T& front() noexcept {
        assert(size > 0);
        return data[0];
    }

    inline const T& front() const noexcept {
        assert(size > 0);
        return data[0];
    }

    inline T& back() noexcept {
        assert(size > 0);
        return data[size - 1];
    }

    inline const T& back() const noexcept {
        assert(size > 0);
        return data[size - 1];
    }

    inline T& at(size_t n) noexcept {
        assert(n < size && "Index out of bounds");
        return data[idx];
    }

    inline const T& at(size_t n) const noexcept {
        assert(n < size && "Index out of bounds");
        return data[idx];
    }

    T& operator[](size_t n) {
        assert(n < size && "Index out of bounds");
        return data[n];
    }

    const T& operator[](size_t n) const {
        assert(n < size && "Index out of bounds");
        return data[n];
    }

    T* begin() { return data; }
    const T* begin() const { return data; }
    T* end() { return data + size; }
    const T* end() const { return data + size; }

};
} // namespace wb