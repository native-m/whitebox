#pragma once

#define NOMINMAX

#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "platform_def.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define WB_UNREACHABLE() __assume(false)
#else
#define WB_UNREACHABLE() __builtin_unreachable()
#endif