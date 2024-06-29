#pragma once

#include "algorithm.h"
#include "common.h"
#include "types.h"
#include <iterator>
#include <memory>
#include <utility>

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

    inline Vector<T>& operator=(const Vector<T>& other)
        requires std::copyable<T>
    {
        if constexpr (std::is_trivially_copyable_v<T>) {
            size_t other_size = other.size();
            if (other_size != size()) {
                destroy_();
                intern_.data = (T*)std::malloc(other_size * sizeof(T));
                intern_.size = other_size;
                intern_.capacity = other_size;
            }
            std::memcpy(intern_.data, other.intern_.data, other_size * sizeof(T));
        } else {
            size_t other_size = other.size();
            if (other_size != size())
                resize(other_size);
            T* data_ptr = intern_.data;
            T* end_ptr = intern_.data + other_size;
            T* other_ptr = other.intern_;
            for (; data_ptr != end_ptr; (void)++data_ptr, (void)++other_ptr)
                *data_ptr = *other_ptr;
        }
        return *this;
    }

    inline Vector<T>& operator=(Vector<T>&& other) noexcept
        requires std::movable<T>
    {
        destroy_();
        intern_ = std::exchange(other.intern_, {});
        return *this;
    }

    /*template <std::input_iterator Iterator>
    inline void assign(Iterator first, Iterator last) {
        if constexpr (std::random_access_iterator<Iterator> && std::contiguous_iterator<Iterator>) {
            auto length = last - first;
            resize(0);
            reserve(length);
            T* data_ptr = intern_.data;

        }
    }*/

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

    inline T* begin() noexcept { return intern_.data; }
    inline const T* begin() const noexcept { return intern_.data; }
    inline T* end() noexcept { return intern_.data + intern_.size; }
    inline const T* end() const noexcept { return intern_.data + intern_.size; }

    template <typename... Args>
    inline T& emplace_at(size_t idx, Args&&... args) {
        if (intern_.size == intern_.capacity) {
            size_t new_capacity = grow_capacity_(intern_.capacity + 1);
            T* new_data = (T*)std::malloc(new_capacity * sizeof(T));
            assert(new_data && "Failed to allocate new storage");
            if (intern_.data) {
                if constexpr (std::is_trivially_copy_constructible_v<T>) {
                    std::memcpy(new_data, intern_.data, idx * sizeof(T));
                    std::memcpy(new_data + idx + 1, intern_.data + idx,
                                (intern_.size - idx) * sizeof(T));
                } else {
                    if constexpr (std::move_constructible<T>) {
                        relocate_by_move(intern_.data, intern_.data + idx, new_data);
                        relocate_by_move(intern_.data + idx, intern_.data + intern_.size,
                                         new_data + idx + 1);
                    } else if constexpr (std::copy_constructible<T>) {
                        relocate_by_copy(intern_.data, intern_.data + idx, new_data);
                        relocate_by_copy(intern_.data + idx, intern_.data + intern_.size,
                                         new_data + idx + 1);
                    }
                }
                std::free(intern_.data);
            }

            T* item = nullptr;
            if constexpr (std::is_aggregate_v<T>) {
                item = new (new_data + idx) T {std::forward<Args>(args)...};
            } else {
                item = new (new_data + idx) T(std::forward<Args>(args)...);
            }

            intern_.data = new_data;
            intern_.capacity = new_capacity;
            intern_.size++;
            return *item;
        }

        T* begin_ptr = intern_.data + intern_.size - 1;
        T* dst_ptr = begin_ptr + 1;
        T* end_ptr = intern_.data + idx;
        while (begin_ptr >= end_ptr) {
            if constexpr (std::movable<T>) {
                *dst_ptr-- = std::move(*begin_ptr);
            } else if constexpr (std::copyable<T>) {
                *dst_ptr-- = *begin_ptr;
            }
            begin_ptr--;
        }

        if (!std::is_trivially_destructible_v<T>) {
            (intern_.data + idx)->~T();
        }

        T* item;
        if constexpr (std::is_aggregate_v<T>) {
            item = new (intern_.data + idx) T {std::forward<Args>(args)...};
        } else {
            item = new (intern_.data + idx) T(std::forward<Args>(args)...);
        }

        intern_.size++;
        return *item;
    }

    template <typename... Args>
    inline T& emplace_back(Args&&... args) {
        if (intern_.size == intern_.capacity) {
            reserve(grow_capacity_(intern_.capacity + 1));
        }
        T* new_item = new (intern_.data + intern_.size) T(std::forward<Args>(args)...);
        intern_.size++;
        return *new_item;
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
        if constexpr (!std::is_trivially_destructible_v<T>)
            intern_.data[intern_.size].~T();
    }

    inline void resize(size_t new_size)
        requires std::default_initializable<T>
    {
        if (new_size > intern_.capacity) {
            reserve(grow_capacity_(new_size));
            uninitialized_default_construct(intern_.data + intern_.size, intern_.data + new_size);
        }
        intern_.size = new_size;
    }

    inline void resize_fast(size_t new_size)
        requires std::is_trivial_v<T>
    {
        if (new_size > intern_.capacity) {
            reserve(grow_capacity_(new_size));
        }
        intern_.size = new_size;
    }

    inline void resize(size_t new_size, T&& default_value)
        requires std::move_constructible<T>
    {
        if (new_size > intern_.capacity) {
            reserve(grow_capacity_(new_size));
            if constexpr (!std::is_trivially_default_constructible_v<T>) {
                T* data_ptr = intern_.data + intern_.size;
                T* end_ptr = intern_.data + new_size;
                while (data_ptr != end_ptr)
                    new (data_ptr++) T(std::move(default_value));
            } else {
                std::memset(intern_.data + intern_.size, 0, (new_size - intern_.size) * sizeof(T));
            }
        }
        intern_.size = new_size;
    }

    inline void reserve(size_t new_capacity) {
        if (new_capacity <= intern_.capacity) {
            return;
        }
        T* new_data = (T*)std::malloc(new_capacity * sizeof(T));
        assert(new_data && "Failed to allocate new storage");
        if (intern_.data) {
            if constexpr (std::is_trivially_copy_constructible_v<T>) {
                if (intern_.size != 0) {
                    std::memcpy(new_data, intern_.data, intern_.size * sizeof(T));
                }
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
        size_t new_capacity =
            intern_.capacity ? (intern_.capacity + intern_.capacity / size_t(2)) : 8;
        return new_capacity > new_size ? new_capacity : new_size;
    }

    inline void destroy_(bool without_free = false) {
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
        if (!without_free) {
            std::free(intern_.data);
            intern_.data = nullptr;
        }
    }
};
} // namespace wb