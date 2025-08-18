#pragma once

#include <string>

#include "core/audio_format.h"
#include "core/common.h"

namespace wb {

enum class ExportBitrateMode {
  CBR,
  ABR,
  VBR,
};

struct ExportAudioProperties {
  enum {
    WAV = 1 << 0,
    AIFF = 1 << 1,
    MP3 = 1 << 2,
    Vorbis = 1 << 3,
    FLAC = 1 << 4,
  };

  uint32_t format_flags = WAV;
  bool export_metadata = true;
  bool override_filename = false;
  std::string filename;
  std::string location;

  // Uncompressed WAV properties
  AudioFormat wav_bit_depth = AudioFormat::I24;

  // Uncompressed AIFF properties
  AudioFormat aiff_bit_depth = AudioFormat::I24;

  // MP3 properties
  ExportBitrateMode mp3_bitrate_mode = ExportBitrateMode::CBR;
  uint32_t mp3_min_bitrate = 32;
  uint32_t mp3_max_bitrate = 320;
  uint32_t mp3_bitrate = 320;
  float mp3_vbr_quality = 100.0f;

  // Ogg Vorbis properties
  ExportBitrateMode vorbis_bitrate_mode = ExportBitrateMode::CBR;
  uint32_t vorbis_min_bitrate = 45;
  uint32_t vorbis_max_bitrate = 500;
  uint32_t vorbis_bitrate = 320;
  float vorbis_vbr_quality = 100.0f;

  // FLAC properties
  AudioFormat flac_bit_depth = AudioFormat::I16;
  int32_t flac_compression_level = 5;
};

}  // namespace wb