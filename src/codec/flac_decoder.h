#pragma once

#include <filesystem>
#include <optional>

#include "codec.h"

namespace wb::codec {

struct FlacFileInfo {
  uint64_t total_samples;
  uint32_t channels;
  uint32_t sample_rate;
  uint32_t bits_per_sample;
};

std::optional<DecodedAudio> decode_flac_file(const std::filesystem::path& path, size_t padding = 0);

std::optional<FlacFileInfo> get_flac_file_info(const std::filesystem::path& path);

}  // namespace wb::codec