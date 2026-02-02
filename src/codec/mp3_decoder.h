#pragma once

#include <filesystem>
#include <optional>

#include "codec.h"

namespace wb::codec {

std::optional<DecodedAudio> decode_mp3_file(const std::filesystem::path& path);

}