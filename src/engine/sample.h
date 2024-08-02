#pragma once

#include "core/audio_format.h"
#include "engine/sample_peaks.h"
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace wb {

struct Sample {
    std::string name;
    std::filesystem::path path;
    AudioFormat format {};
    uint8_t channels {};
    uint32_t sample_rate {};
    size_t count {};
    size_t byte_length {};
    std::vector<std::byte*> sample_data;

    Sample() {}
    Sample(Sample&& other) noexcept;
    ~Sample();

    template <typename T>
    inline const T* get_read_pointer(uint32_t channel) const noexcept {
        if (channel > channels)
            return nullptr;
        return (T*)sample_data[channel];
    }

    template <typename T>
    inline T* get_write_pointer(uint32_t channel) noexcept {
        if (channel > channels)
            return nullptr;
        return (T*)sample_data[channel];
    }

    // Summarize sample for visualization mipmap
    bool summarize_for_mipmaps(SamplePeaksPrecision precision, uint32_t channel, uint32_t mip_level,
                               size_t output_offset, size_t* output_count, void* output_data) const;

    static std::optional<Sample> load_file(const std::filesystem::path& path) noexcept;

    static std::optional<Sample> load_compressed_file(const std::filesystem::path& path) noexcept;

    static std::optional<Sample> load_mp3_file(const std::filesystem::path& path) noexcept;

    static std::optional<Sample> load_flac_file(const std::filesystem::path& path) noexcept;

    static std::optional<Sample> load_ogg_vorbis_file(const std::filesystem::path& path) noexcept;
};
} // namespace wb