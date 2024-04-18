#pragma once

namespace wb {
template <typename T, typename... Args>
inline constexpr bool has_bit_enum(T op, Args... bits) {
    T mask = ((1 << (T)bits) | ...);
    return (op & mask) != 0;
}
} // namespace wb