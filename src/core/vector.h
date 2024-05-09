#pragma once

#include "common.h"
#include "types.h"
#include <memory>

namespace wb {
namespace detail {
template <typename T>
struct VectorInternal {
    T* data;
    size_t size;
    size_t capacity;
};
} // namespace detail

template <typename T>
struct Vector {
    detail::VectorInternal<T> intern_ {};

    Vector() noexcept {}
    Vector(size_t size) { resize(size); }
    Vector(Vector<T>&& other) noexcept : intern_(std::exchange(other.intern_, {})) {}
    Vector(const Vector<T>&) = delete;
    ~Vector() { destroy_(); }

    inline bool empty() const { return intern_.size == 0; }
    inline size_t size() const { return intern_.size; }
    inline size_t max_size() const { return SIZE_MAX / sizeof(T); }
    inline size_t capacity() const { return intern_.capacity; }
    inline T* data() { return intern_.data; }
    inline const T* data() const { return intern_.data; }

    inline T& front() noexcept {
        assert(intern_.size > 0);
        return intern_.data[0];
    }

    inline const T& front() const noexcept {
        assert(intern_.size > 0);
        return intern_.data[0];
    }

    inline T& back() noexcept {
        assert(intern_.size > 0);
        return intern_.data[intern_.size - 1];
    }

    inline const T& back() const noexcept {
        assert(intern_.size > 0);
        return intern_.data[intern_.size - 1];
    }

    inline T& operator[](size_t idx) noexcept {
        assert(idx >= 0 && idx < intern_.size);
        return intern_.data[idx];
    }

    inline const T& operator[](size_t idx) const noexcept {
        assert(idx >= 0 && idx < intern_.size);
        return intern_.data[idx];
    }

    inline void push_back(const T& item)
        requires std::copy_constructible<T>
    {
        if (intern_.size == intern_.capacity) {
            reserve(grow_capacity_(intern_.capacity + 1));
        }
        new (intern_.data + intern_.size) T(item);
        intern_.size++;
    }

    inline void push_back(T&& item)
        requires std::move_constructible<T>
    {
        if (intern_.size == intern_.capacity) {
            reserve(grow_capacity_(intern_.capacity + 1));
        }
        new (intern_.data + intern_.size) T(std::move(item));
        intern_.size++;
    }

    inline void pop_back() noexcept {
        assert(intern_.size > 0);
        intern_.size--;
    }

    inline void resize(size_t new_size) {
        if (new_size > intern_.capacity) {
            reserve(grow_capacity_(new_size));
            if constexpr (!std::is_trivially_default_constructible_v<T>) {
                T* data_ptr = intern_.data + intern_.size;
                T* end_ptr = intern_.data + new_size;
                while (data_ptr != end_ptr)
                    new (data_ptr++) T();
            } else {
                std::memset(intern_.data, 0, (new_size - intern_.size) * sizeof(T));
            }
        }
        intern_.size = new_size;
    }

    inline void reserve(size_t new_capacity) {
        if (new_capacity <= intern_.capacity) {
            return;
        }
        T* new_data = (T*)std::malloc(new_capacity * sizeof(T));
        assert(new_data);
        if (intern_.data) {
            if constexpr (std::is_trivially_copy_constructible_v<T>) {
                std::memcpy(new_data, intern_.data, intern_.size * sizeof(T));
            } else {
                T* new_data_ptr = new_data;
                T* old_data_ptr = intern_.data;
                T* end_ptr = old_data_ptr + intern_.size;
                if constexpr (std::move_constructible<T>) {
                    while (old_data_ptr != end_ptr) {
                        new (new_data_ptr++) T(std::move(*old_data_ptr));
                        if constexpr (!std::is_trivially_destructible_v<T>)
                            old_data_ptr->~T();
                        old_data_ptr++;
                    }
                } else if constexpr (std::copy_constructible<T>) {
                    while (old_data_ptr != end_ptr) {
                        new (new_data_ptr++) T(*old_data_ptr);
                        if constexpr (!std::is_trivially_destructible_v<T>)
                            old_data_ptr->~T();
                        old_data_ptr++;
                    }
                }
            }
            std::free(intern_.data);
        }
        intern_.data = new_data;
        intern_.capacity = new_capacity;
    }

    inline size_t grow_capacity_(size_t new_size) {
        size_t new_capacity = intern_.capacity ? (intern_.capacity + intern_.capacity / size_t(2)) : 8;
        return new_capacity > new_size ? new_capacity : new_size;
    }

    inline void destroy_() {
        if (!intern_.data)
            return;
        if constexpr (!std::is_trivially_destructible_v<T>) {
            T* data_ptr = intern_.data;
            T* end_ptr = data_ptr + intern_.size;
            while (data_ptr != end_ptr) {
                data_ptr->~T();
                data_ptr++;
            }
        }
        std::free(intern_.data);
        intern_.data = nullptr;
    }
};
} // namespace wb