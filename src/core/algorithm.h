#pragma once

#include <cstring>
#include <iterator>
#include <type_traits>

namespace wb {

template<typename T, typename Tcmp, typename... Args>
inline bool any_of(T value, Tcmp cmp, Args... cmp_args) noexcept {
  return value == cmp || ((value == cmp_args) || ...);
}

template<typename T, typename ValT, typename CompareFn>
inline T find_lower_bound(T begin, T end, ValT value, CompareFn comp_fn) noexcept {
  using DiffType = typename std::iterator_traits<T>::difference_type;
  DiffType left = 0;
  DiffType right = (end - begin) - 1;

  while (left < right) {
    DiffType middle = (left + right) >> 1;  // In case of compiler being stupid
    if (comp_fn(begin[middle], value)) {
      left = middle + 1;
    } else {
      right = middle;
    }
  }

  return begin + right;
}

template<typename T>
inline void relocate_by_copy(T* begin_ptr, T* end_ptr, T* dst_ptr) {
  while (begin_ptr != end_ptr) {
    new (dst_ptr++) T(*begin_ptr);
    if constexpr (!std::is_trivially_destructible_v<T>)
      begin_ptr->~T();
    begin_ptr++;
  }
}

template<typename T>
inline void relocate_by_move(T* begin_ptr, T* end_ptr, T* dst_ptr) {
  while (begin_ptr != end_ptr) {
    new (dst_ptr++) T(std::move(*begin_ptr));
    if constexpr (!std::is_trivially_destructible_v<T>)
      begin_ptr->~T();
    begin_ptr++;
  }
}

template<typename T>
inline void uninitialized_default_construct(T* begin_ptr, T* end_ptr) {
  if constexpr (!std::is_trivially_default_constructible_v<T>) {
    while (begin_ptr != end_ptr)
      new (begin_ptr++) T();
  } else {
    std::memset(begin_ptr, 0, (end_ptr - begin_ptr) * sizeof(T));
  }
}

template<typename T>
inline void destroy_range(T* begin_ptr, T* end_ptr) {
  if constexpr (!std::is_trivially_default_constructible_v<T>) {
    while (begin_ptr != end_ptr) {
      begin_ptr->~T();
      begin_ptr++;
    }
  }
}

}  // namespace wb