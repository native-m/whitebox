#include "mem_info.h"

#if defined(WB_PLATFORM_WINDOWS)
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#elif defined(WB_PLATFORM_MACOS)
#include <mach/mach.h>
#endif

namespace wb {
uint64_t get_max_memory() {
  return 0;
}

MemoryInfo get_app_memory_info() {
#if defined(WB_PLATFORM_WINDOWS)
  HANDLE current_process = GetCurrentProcess();
  PROCESS_MEMORY_COUNTERS_EX memory_counters;
  GetProcessMemoryInfo(current_process, (PPROCESS_MEMORY_COUNTERS)&memory_counters, sizeof(memory_counters));
  return {
    .overall_usage = memory_counters.PrivateUsage,
    .physical_usage = memory_counters.WorkingSetSize,
  };
#elif defined(WB_PLATFORM_LINUX)
  return {};
#elif defined(WB_PLATFORM_MACOS)
  task_basic_info_data_t info;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  auto task_self = mach_task_self();
  task_info(task_self, TASK_BASIC_INFO, (task_info_t)&info, &count);
  return {
    .overall_usage = info.virtual_size / 1024,
    .physical_usage = info.resident_size,
  };
#else
  return {};
#endif
}
}  // namespace wb