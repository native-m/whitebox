#pragma once

#include "core/common.h"

namespace wb {
struct MemoryInfo {
    uint64_t overall_usage;
    uint64_t physical_usage;
};

uint64_t get_max_memory();
MemoryInfo get_app_memory_info();
} // namespace wb