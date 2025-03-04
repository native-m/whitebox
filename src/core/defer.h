#pragma once

template <typename Fn>
struct __WBDefer {
    Fn defer_fn;
    constexpr __WBDefer(Fn&& fn) : defer_fn(fn) {}
    constexpr __WBDefer(__WBDefer&&) = delete;
    constexpr __WBDefer(const __WBDefer&) = delete;
    constexpr __WBDefer& operator=(__WBDefer&&) = delete;
    constexpr __WBDefer& operator=(const __WBDefer&) = delete;
    constexpr ~__WBDefer() { defer_fn(); }
};

template <typename Fn>
__WBDefer(Fn&&) -> __WBDefer<Fn>;

#define var_defer__(x) __defer__##x
#define var_defer_(x) var_defer__(x)
#define defer(x) __WBDefer var_defer_(__COUNTER__)([&] { x; })
#define defer_block(x) __WBDefer var_defer_(__COUNTER__)(x)