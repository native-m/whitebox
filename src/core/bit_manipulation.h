#pragma once

#include <bit>

namespace wb {
template <typename T, typename... Args>
inline constexpr bool has_bit_enum(T op, Args... bits) {
    T mask = ((1 << (T)bits) | ...);
    return (op & mask) != 0;
}

template <typename T, typename... Args>
inline constexpr bool has_bit(T op, Args... bits) {
    T mask = (bits | ...);
    return (op & mask) != 0;
}

template <typename T, typename... Args>
inline constexpr bool contain_bit(T op, Args... bits) {
    T mask = (bits | ...);
    return (op & mask) == mask;
}

template <typename T>
inline constexpr int next_set_bits(T& x) {
    T t = x & -x;
    int r = std::countr_zero(x);
    x ^= t;
    return r;
}
} // namespace wb