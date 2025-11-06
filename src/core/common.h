#pragma once

#define NOMINMAX

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "platform_def.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define WB_UNREACHABLE() __assume(false)
#define WB_RESTRICT_FN   __declspec(restrict)
#define WB_RESTRICT      __restrict
#else
#define WB_UNREACHABLE() __builtin_unreachable()
#define WB_RESTRICT_FN 
#define WB_RESTRICT    __restrict
#endif
