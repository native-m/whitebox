#pragma once

#define NOMINMAX

#include "platform_def.h"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cassert>

#if defined(_MSC_VER) && !defined(__clang__)
#define WB_UNREACHABLE() __assume(false)
#else
#define WB_UNREACHABLE() __builtin_unreachable()
#endif
