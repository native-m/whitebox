#pragma once
#include "../types.h"

namespace wb
{
    enum class AudioFormat : uint8_t
    {
        Unknown = 0,
        I8,
        I16,
        I24,
        I32,
        F32,
        F64,
    };

    inline static bool is_integer_format(AudioFormat format)
    {
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

    inline static bool is_floating_point_format(AudioFormat format)
    {
        switch (format) {
            case AudioFormat::F32:  return true;
            case AudioFormat::F64:  return true;
            default:                break;
        }

        return false;
    }

    inline static uint32_t get_audio_sample_size(AudioFormat format)
    {
        switch (format) {
            case AudioFormat::I8:   return sizeof(int8_t);
            case AudioFormat::I16:  return sizeof(int16_t);
            case AudioFormat::I24:  return sizeof(int32_t);
            case AudioFormat::I32:  return sizeof(int32_t);
            case AudioFormat::F32:  return sizeof(float);
            case AudioFormat::F64:  return sizeof(double);
            default:                break;
        }

        return 0;
    }
}