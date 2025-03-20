#pragma once

#include <concepts>
#include <type_traits>

namespace wb {
template<typename T>
concept Trivial = std::is_trivial_v<T>;
template<typename T>
concept NumericalType = std::floating_point<T> || std::integral<T>;
template<typename T>
concept ReferenceType = std::is_reference_v<T>;
template<typename T>
concept PointerType = std::is_pointer_v<T>;

template<typename T>
concept DynamicArrayContainer = requires(T v) {
  { v.at(size_t()) } -> ReferenceType;
  { v[size_t()] } -> ReferenceType;
  { v.data() } -> PointerType;
  { v.size() } -> std::convertible_to<size_t>;
  v.resize(size_t());
  v.reserve(size_t());
};

template<typename T>
concept ContinuousArrayContainer = requires(T v) {
  { v.data() } -> PointerType<>;
  { v.size() } -> std::convertible_to<size_t>;
};

template<typename T1, typename T2>
struct Pair {
  T1 first;
  T2 second;
};

}  // namespace wb