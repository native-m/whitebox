#pragma once

#include <type_traits>
#include <cstring>

namespace wb {

template <typename T>
inline void relocate_by_copy(T* begin_ptr, T* end_ptr, T* dst_ptr) {
    while (begin_ptr != end_ptr) {
        new (dst_ptr++) T(*begin_ptr);
        if constexpr (!std::is_trivially_destructible_v<T>)
            begin_ptr->~T();
        begin_ptr++;
    }
}

template <typename T>
inline void relocate_by_move(T* begin_ptr, T* end_ptr, T* dst_ptr) {
    while (begin_ptr != end_ptr) {
        new (dst_ptr++) T(std::move(*begin_ptr));
        if constexpr (!std::is_trivially_destructible_v<T>)
            begin_ptr->~T();
        begin_ptr++;
    }
}

template <typename T>
inline void uninitialized_default_construct(T* begin_ptr, T* end_ptr) {
    if constexpr (!std::is_trivially_default_constructible_v<T>) {
        while (begin_ptr != end_ptr)
            new (begin_ptr++) T();
    } else {
        std::memset(begin_ptr, 0, (end_ptr - begin_ptr) * sizeof(T));
    }
}

} // namespace wb