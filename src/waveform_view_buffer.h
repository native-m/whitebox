#pragma once

#include <cstdint>

namespace wb
{
    struct SamplePeaks
    {
        uint32_t sample_count;
        uint32_t num_channels;
        virtual ~SamplePeaks() { }
    };
}