#pragma once

#include <concepts>
#include <type_traits>

namespace wb {
template <typename T>
concept Trivial = std::is_trivial_v<T>;
}