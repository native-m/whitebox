#pragma once

#define WIN32_LEAN_AND_MEAN 1
#define NOMINMAX 1

#if defined(_MSC_VER)
#define WB_UNREACHABLE() __assume(false)
#elif defined(__GNUC__) || defined(__clang__)
#define WB_UNREACHABLE() __builtin_unreachable()
#endif

#define WB_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))