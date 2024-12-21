#pragma once

#include "core/common.h"

namespace wb {
enum class TrackInputMode {
    None,
    Midi,
    ExternalStereo,
    ExternalMono,
};

struct TrackInput {
    TrackInputMode mode;
    uint32_t index;

    inline uint32_t as_packed_u32() const {
        return (index & 0xFFFFFFu) | ((static_cast<uint32_t>(mode) & 0xFFu) << 24);
    }

    inline static TrackInput from_packed_u32(uint32_t u32) {
        return {
            .mode = static_cast<TrackInputMode>(u32 >> 24u),
            .index = u32 & 0xFFFFFFu,
        };
    }
};

} // namespace wb