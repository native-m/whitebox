#pragma once

#include "../types.h"
#include "../core/audio_format.h"
#include <filesystem>
#include <optional>

namespace wb
{
    struct Sample
    {
        std::string             name;
        std::filesystem::path   path;
        AudioFormat             format{};
        uint16_t                channels{};
        uint32_t                sample_rate{};
        size_t                  sample_length{};
        size_t                  byte_length{};
        std::vector<std::byte*> sample_data_;


        Sample() = default;

        Sample(Sample&& other) noexcept;

        ~Sample();

        // Load sample from file
        static std::optional<Sample> load_file(const std::filesystem::path& path) noexcept;

        // Stream sample from file
        static std::optional<Sample> stream_file(const std::filesystem::path& path) noexcept;

        // Make a copy of a sample
        static std::optional<Sample> copy(const Sample& other) noexcept;
    };
}