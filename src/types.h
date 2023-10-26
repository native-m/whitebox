#pragma once
#include "def.h"
#include <cstdint>
#include <cassert>
#include <concepts>

#undef min
#undef max

#define BOOST_NO_EXCEPTIONS 1

#ifdef _NDEBUG
#define WB_ASSERT(x)
#else
#define WB_ASSERT(x) assert(x)
#endif

namespace wb
{
    template<typename T, typename... Types>
    concept Any = (std::same_as<T, Types> || ...);

    template<typename T, typename... Types>
    concept All = (std::same_as<T, Types> && ...);

    template<typename T>
    concept ArithmeticType = std::is_arithmetic_v<T>;
}
