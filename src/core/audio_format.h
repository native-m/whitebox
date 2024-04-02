#pragma once

#include "common.h"

namespace wb {
enum class AudioFormat : uint8_t {
    Unknown,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    F32,
    F64,
};

inline static uint32_t get_audio_format_size(AudioFormat format) {
    switch (format) {
        case AudioFormat::I8:
        case AudioFormat::U8:
            return 1;
        case AudioFormat::I16:
        case AudioFormat::U16:
            return 2;
        case AudioFormat::I32:
        case AudioFormat::U32:
        case AudioFormat::F32:
            return 4;
        case AudioFormat::F64:
            return 8;
        default:
            break;
    }
    
    return 0;
}

} // namespace wb