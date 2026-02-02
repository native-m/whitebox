#pragma once

#include <filesystem>
#include <optional>

#include "codec.h"

namespace wb::codec {

std::optional<DecodedAudio> decode_ogg_vorbis_file(const std::filesystem::path& path);

}