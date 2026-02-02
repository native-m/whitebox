#include "ogg_decoder.h"

#include <vorbis/vorbisfile.h>

#include "core/core_math.h"
#include "core/debug.h"
#include "core/defer.h"

namespace wb::codec {

std::optional<DecodedAudio> decode_ogg_vorbis_file(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  std::u8string str_path = path.generic_u8string();
  OggVorbis_File vf;
  if (ov_fopen((const char*)str_path.c_str(), &vf) != 0)
    return {};
  defer(ov_clear(&vf));

  vorbis_info* info = ov_info(&vf, -1);
  int channels = math::min(info->channels, 32);
  uint64_t buffer_len_per_channel = 1024;
  uint64_t total_frame_count = ov_pcm_total(&vf, -1);
  Vector<std::byte*> channel_samples;
  channel_samples.reserve(info->channels);

  for (uint32_t i = 0; i < info->channels; i++) {
    std::byte* mem = (std::byte*)std::malloc(total_frame_count * sizeof(float));
    if (!mem) {
      for (auto sample_data : channel_samples)
        std::free(sample_data);
      return {};
    }
    channel_samples.push_back(mem);
  }

  uint64_t num_frames_written = 0;
  int current_bitstream = 0;
  float** decode_channels = nullptr;
  while (true) {
    int ret = ov_read_float(&vf, &decode_channels, buffer_len_per_channel, &current_bitstream);
    if (ret == 0) {
      break;
    } else if (ret < 0) {
      Log::error("Failed to decode Ogg Vorbis file. ov_read_float() returned {}", ret);
      break;
    }
    for (int c = 0; c < channels; c++) {
      float* channel_data = (float*)channel_samples[c];
      std::memcpy(channel_data + num_frames_written, decode_channels[c], ret * sizeof(float));
    }
    num_frames_written += ret;
  }

  return DecodedAudio{ .format = AudioFormat::F32,
                       .channels = (uint32_t)channels,
                       .sample_rate = (uint32_t)info->rate,
                       .total_samples = total_frame_count,
                       .channel_data = std::move(channel_samples) };
}

}  // namespace wb::codec