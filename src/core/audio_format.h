#pragma once

#include "common.h"

namespace wb {
enum class AudioFormat : uint8_t {
    Unknown,
    I8,
    U8,
    I16,
    U16,
    I24,
    I24_X8, // 24-bit format on 4-byte container
    I32,
    U32,
    F32,
    F64,
    Max,
};

inline static uint32_t get_audio_format_size(AudioFormat format) {
    switch (format) {
        case AudioFormat::I8:
        case AudioFormat::U8:
            return 1;
        case AudioFormat::I16:
        case AudioFormat::U16:
            return 2;
        case AudioFormat::I24:
            return 3;
        case AudioFormat::I24_X8:
            return 4;
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

inline static bool is_signed_integer_format(AudioFormat format) {
    switch (format) {
        case AudioFormat::I8:
        case AudioFormat::I16:
        case AudioFormat::I24:
        case AudioFormat::I32:
            return true;
        default:
            break;
    }
    return false;
}

inline static bool is_floating_point_format(AudioFormat format) {
    switch (format) {
        case AudioFormat::F32:
        case AudioFormat::F64:
            return true;
        default:
            break;
    }
    return false;
}

inline static const char* get_audio_format_string(AudioFormat format) {
    switch (format) {
        case AudioFormat::I8:
            return "8-bit int";
        case AudioFormat::U8:
            return "8-bit uint";
        case AudioFormat::I16:
            return "16-bit int";
        case AudioFormat::U16:
            return "16-bit uint";
        case AudioFormat::I24:
            return "24-bit int";
        case AudioFormat::I24_X8:
            return "24-bit int (4 bytes)";
        case AudioFormat::I32:
            return "32-bit int";
        case AudioFormat::U32:
            return "32-bit uint";
        case AudioFormat::F32:
            return "32-bit float";
    }
    return "Unknown Format";
}

} // namespace wb