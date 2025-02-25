#include "mem_info.h"

#if defined(WB_PLATFORM_WINDOWS)
#include <Windows.h>
#include <Psapi.h>
#endif

namespace wb {
static HANDLE current_process = GetCurrentProcess();

uint64_t get_max_memory() {
    return 0;
}

MemoryInfo get_app_memory_info() {
#if defined(WB_PLATFORM_WINDOWS)
    PROCESS_MEMORY_COUNTERS_EX memory_counters;
    GetProcessMemoryInfo(current_process, (PPROCESS_MEMORY_COUNTERS)&memory_counters, sizeof(memory_counters));
    return {
        .overall_usage = memory_counters.PrivateUsage,
        .physical_usage = memory_counters.WorkingSetSize
    };
#elif defined(WB_PLATFORM_LINUX)
    return {};
#else
    return {};
#endif
}
} // namespace wb