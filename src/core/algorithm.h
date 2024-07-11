#pragma once

#include <cstring>
#include <type_traits>
#include <iterator>

namespace wb {

template <typename T, typename Tcmp, typename... Args>
inline bool any_of(T value, Tcmp cmp, Args... cmp_args) {
    return value == cmp || ((value == cmp_args) || ...);
}

template <typename T, typename ValT, typename CompareFn>
inline T binary_search(T begin, T end, ValT value, CompareFn comp_fn) {
    using DiffType = typename std::iterator_traits<T>::difference_type;
    DiffType left = -1;
    DiffType right = end - begin;

    while (right - left > 1) {
        DiffType middle = (left + right) >> 1;
        DiffType& side = (comp_fn(begin[middle], value) ? left : right);
        side = middle;
    }

    return begin + right;
}

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