#pragma once

template <typename Fn>
struct Defer {
    Fn defer_fn;
    constexpr Defer(Defer&&) = delete;
    constexpr Defer(const Defer&) = delete;
    constexpr Defer& operator=(Defer&&) = delete;
    constexpr Defer& operator=(const Defer&) = delete;
    constexpr Defer(Fn&& fn) : defer_fn(fn) {}
    constexpr ~Defer() { defer_fn(); }
};

template <typename Fn>
Defer(Fn&&) -> Defer<Fn>;

#define var_defer__(x) __defer__##x
#define var_defer_(x) var_defer__(x)
#define defer(x) Defer var_defer_(__COUNTER__)([&] { x; })
#define defer_block(x) Defer var_defer_(__COUNTER__)(x)