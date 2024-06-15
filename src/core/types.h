#pragma once

#include <concepts>
#include <type_traits>

namespace wb {
template <typename T>
concept Trivial = std::is_trivial_v<T>;
template <typename T>
concept NumericalType = std::floating_point<T> || std::integral<T>;
}