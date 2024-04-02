#pragma once

#include "core/common.h"

namespace wb {

enum class SamplePeaksPrecision {
    Low,
    High,
};

struct SamplePeaks {
    size_t sample_count;
    uint32_t mipmap_count;
    uint32_t channels;
    SamplePeaksPrecision precision;
    bool cpu_accessible;
    virtual ~SamplePeaks() {}
};

} // namespace wb