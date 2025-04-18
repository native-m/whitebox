#include "thread.h"

#include <thread>

#include "debug.h"

#ifdef WB_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace wb {

#ifdef WB_PLATFORM_WINDOWS
thread_local HANDLE global_waitable_timer{};
#endif

void accurate_sleep_ns(int64_t timeout_ns) {
#ifdef WB_PLATFORM_WINDOWS
  uint32_t flags = CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION;
  if (!global_waitable_timer)
    global_waitable_timer = CreateWaitableTimerEx(nullptr, nullptr, flags, TIMER_ALL_ACCESS);
  LARGE_INTEGER due_time;
  due_time.QuadPart = -(int64_t)(timeout_ns / 100);
  SetWaitableTimer(global_waitable_timer, &due_time, 0, nullptr, nullptr, FALSE);
  WaitForSingleObjectEx(global_waitable_timer, INFINITE, FALSE);
#else
  std::this_thread::sleep_for(std::chrono::nanoseconds(timeout_ns));
  // TODO: Implement for other platforms
#endif
}

}  // namespace wb