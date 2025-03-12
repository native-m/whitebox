#pragma once

#include "core/audio_format.h"
#include "core/common.h"

namespace wb {

enum class ExportBitrateMode {
  CBR,
  ABR,
  VBR,
};

struct ExportAudioProperties {
  bool enable_wav = true;
  bool enable_aiff = false;
  bool enable_mp3 = false;
  bool enable_vorbis = false;
  bool enable_flac = false;
  bool export_metadata = true;

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