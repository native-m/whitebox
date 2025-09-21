#include "timing.h"

#ifdef WB_PLATFORM_WINDOWS
#include <Windows.h>
#elif defined(WB_PLATFORM_LINUX)
#include <time.h>
#elif defined(WB_PLATFORM_MACOS)
#include <mach/mach_time.h>
#endif

namespace wb {

uint64_t tm_get_ticks() {
#if defined(WB_PLATFORM_WINDOWS)
  LARGE_INTEGER perf_count;
  QueryPerformanceCounter(&perf_count);
  return perf_count.QuadPart;
#elif defined(WB_PLATFORM_LINUX)
  uint64_t ticks;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  ticks = now.tv_sec * 1000000000ull;
  ticks += now.tv_nsec;
  return ticks;
#elif defined(WB_PLATFORM_MACOS)
  return mach_absolute_time();
#endif
}

#if defined(WB_PLATFORM_WINDOWS)
static uint64_t perf_freq = []() -> uint64_t {
  LARGE_INTEGER perf_freq;
  QueryPerformanceFrequency(&perf_freq);
  return perf_freq.QuadPart;
}();
#endif

uint64_t tm_get_ticks_per_seconds() {
#if defined(WB_PLATFORM_WINDOWS)
  return perf_freq;
#elif defined(WB_PLATFORM_LINUX)
  return 1000000000;
#else
  mach_timebase_info_data_t info;
  mach_timebase_info(&info);
  return (1000000000ULL * (uint64_t)info.denom) / (uint64_t)info.numer;
#endif
}

}  // namespace wb