#include "mp3_decoder.h"

#include "core/debug.h"
#include "extern/dr_mp3.h"

namespace wb::codec {

template<typename T>
static uint64_t
deinterleave_samples(Vector<std::byte*>& dst, const T* src, uint64_t num_read, uint64_t num_frames_written, int channels) {
  for (int i = 0; i < channels; i++) {
    T* channel_data = (T*)dst[i];
    for (uint64_t j = 0; j < num_read; j++)
      channel_data[num_frames_written + j] = src[channels * j + i];
  }
  return num_frames_written + num_read;
}

std::optional<DecodedAudio> decode_mp3_file(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  drmp3 mp3_file;
  std::u8string str_path = path.generic_u8string();
  if (!drmp3_init_file(&mp3_file, (const char*)str_path.c_str(), nullptr)) {
    return {};
  }

  uint64_t buffer_len_per_channel = 1024;
  float* decode_buffer = (float*)std::malloc(buffer_len_per_channel * mp3_file.channels * sizeof(float));
  if (!decode_buffer) {
    drmp3_uninit(&mp3_file);
    return {};
  }

  uint64_t total_frame_count = drmp3_get_pcm_frame_count(&mp3_file);
  Vector<std::byte*> channel_samples;
  channel_samples.reserve(mp3_file.channels);

  for (uint32_t i = 0; i < mp3_file.channels; i++) {
    std::byte* mem = (std::byte*)std::malloc(total_frame_count * sizeof(float));
    if (!mem) {
      for (auto sample_data : channel_samples)
        std::free(sample_data);
      std::free(decode_buffer);
      drmp3_uninit(&mp3_file);
      return {};
    }
    channel_samples.push_back(mem);
  }

  uint64_t num_frames_read = 0;
  uint64_t num_frames_written = 0;
  while (true) {
    num_frames_read = drmp3_read_pcm_frames_f32(&mp3_file, buffer_len_per_channel, decode_buffer);
    if (num_frames_read == 0)
      break;
    num_frames_written =
        deinterleave_samples(channel_samples, decode_buffer, num_frames_read, num_frames_written, mp3_file.channels);
  }

  uint32_t sample_rate = mp3_file.sampleRate;
  uint32_t channels = mp3_file.channels;

  drmp3_uninit(&mp3_file);
  std::free(decode_buffer);

  return DecodedAudio{ .format = AudioFormat::F32,
                       .channels = channels,
                       .sample_rate = sample_rate,
                       .total_samples = total_frame_count,
                       .channel_data = std::move(channel_samples) };
}

}  // namespace wb::codec