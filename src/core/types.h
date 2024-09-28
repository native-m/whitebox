#pragma once

#include <concepts>
#include <type_traits>

namespace wb {
template <typename T>
concept Trivial = std::is_trivial_v<T>;
template <typename T>
concept NumericalType = std::floating_point<T> || std::integral<T>;
template <typename T>
concept IsReferenceType = std::is_reference_v<T>;
template <typename T>
concept IsPointerType = std::is_pointer_v<T>;

template <typename T>
concept DynamicArrayContainer = requires(T v) {
    { v.at(size_t()) } -> IsReferenceType;
    { v[size_t()] } -> IsReferenceType;
    { v.data() } -> IsPointerType;
    { v.size() } -> std::convertible_to<size_t>;
    v.resize(size_t());
    v.reserve(size_t());
};

template <typename T>
concept ContinuousArrayContainer = requires(T v) {
    { v.data() } -> IsPointerType<>;
    { v.size() } -> std::convertible_to<size_t>;
};

}