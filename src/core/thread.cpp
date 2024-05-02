#include "thread.h"
#include "debug.h"
#include <thread>

#ifdef WB_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace wb {

thread_local HANDLE global_waitable_timer {};

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
    // TODO: Implement for other platforms
#endif
}

} // namespace wb