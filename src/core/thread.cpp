#include "thread.h"

#include <thread>

#include "debug.h"

#ifdef WB_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace wb {

#ifdef WB_PLATFORM_WINDOWS
#define MS_VC_EXCEPTION 0x406D1388

struct THREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000
  LPCSTR szName;     // Pointer to name (in user address space)
  DWORD dwThreadID;  // Thread ID (-1 for caller thread)
  DWORD dwFlags;     // Reserved for future use; must be zero
};

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

void set_current_thread_name(const char* name) {
#ifdef WB_PLATFORM_WINDOWS
  THREADNAME_INFO info{
    .dwType = 0x1000,
    .szName = name,
    .dwThreadID = (DWORD)-1,
  };
  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
  } __except (GetExceptionCode() == MS_VC_EXCEPTION ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_EXECUTE_HANDLER) {
  }
#endif
}

}  // namespace wb