#pragma once

#include <cstring>
#include <iterator>
#include <type_traits>

#ifdef _MSC_VER
#define WB_BUILTIN_MEMCPY(dst, src, size)  memcpy((dst), (src), (size))
#define WB_BUILTIN_MEMMOVE(dst, src, size) memmove((dst), (src), (size))
#define WB_BUILTIN_MEMSET(dst, val, size)  memset((dst), (val), (size))
#else
#define WB_BUILTIN_MEMCPY(dst, src, size)  __builtin_memcpy((dst), (src), (size))
#define WB_BUIlTIN_MEMMOVE(dst, src, size) __builtin_memmove((dst), (src), (size))
#define WB_BUILTIN_MEMSET(dst, val, size)  __builtin_memset((dst), (val), (size))
#endif

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

template<typename T>
inline void default_initialize_n(T* data, size_t count) {
  if constexpr (std::is_trivially_default_constructible_v<T>) {
    WB_BUILTIN_MEMSET(data, 0, count * sizeof(T));
  } else {
    for (size_t i = 0; i < count; i++) {
      new (&data[i]) T();
    }
  }
}

template<typename T>
inline void copy_n(T* dst, T* src, size_t count) {
  if constexpr (std::is_trivially_copy_assignable_v<T>) {
    WB_BUILTIN_MEMCPY(dst, src, count * sizeof(T));
  } else {
    for (size_t i = 0; i < count; i++) {
      dst[i] = src[i];
    }
  }
}

template<typename T>
inline void move_n(T* dst, T* src, size_t count) {
  if constexpr (std::is_trivially_copy_assignable_v<T>) {
    WB_BUILTIN_MEMCPY(dst, src, count * sizeof(T));
  } else {
    for (size_t i = 0; i < count; i++) {
      dst[i] = std::move(src[i]);
    }
  }
}

template<typename T>
inline void fill_n(T* dst, const T& value, size_t count) {
  for (size_t i = 0; i < count; i++) {
    new (&dst[i]) T(value);
  }
}

template<typename T>
inline void fill_n(T* dst, T&& value, size_t count) {
  for (size_t i = 0; i < count; i++) {
    new (&dst[i]) T(std::move(value));
  }
}

template<typename T>
inline void copy_initialize_n(T* dst, const T* src, size_t count) {
  if constexpr (std::is_trivially_copy_constructible_v<T>) {
    WB_BUILTIN_MEMCPY(dst, src, count * sizeof(T));
  } else {
    for (size_t i = 0; i < count; i++) {
      new (&dst[i]) T(src[i]);
    }
  }
}

template<typename T>
inline void move_initialize_n(T* dst, T* src, size_t count) {
  if constexpr (std::is_trivially_move_constructible_v<T>) {
    WB_BUILTIN_MEMCPY(dst, src, count * sizeof(T));
  } else {
    for (size_t i = 0; i < count; i++) {
      new (&dst[i]) T(std::move(src[i]));
    }
  }
}

template<typename T>
inline void destroy_n(T* data, size_t count) {
  if constexpr (!std::is_trivially_destructible_v<T>) {
    for (size_t i = 0; i < count; i++)
      data[i].~T();
  }
}

}  // namespace wb