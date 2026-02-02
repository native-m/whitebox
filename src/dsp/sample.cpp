#include "sample.h"

#include <sndfile.h>
#include <utility>

#include "codec/codec.h"
#include "core/core_math.h"
#include "core/debug.h"

namespace wb {

static AudioFormat from_sf_format(int sf_format) {
  // Only supports uncompressed format
  switch (sf_format) {
    case SF_FORMAT_PCM_16: return AudioFormat::I16;
    case SF_FORMAT_PCM_24: return AudioFormat::I32;  // Converted to 32-bit
    case SF_FORMAT_PCM_32: return AudioFormat::I32;
    case SF_FORMAT_FLOAT: return AudioFormat::F32;
    case SF_FORMAT_DOUBLE: return AudioFormat::F64;
    default: break;
  }
  return {};
}

template<typename T>
static sf_count_t deinterleave_samples(
    Vector<std::byte*>& dst,
    const T* src,
    sf_count_t num_read,
    sf_count_t dst_frames,
    sf_count_t num_frames_written,
    int channels) {
  for (int i = 0; i < channels; i++) {
    T* channel_data = (T*)dst[i];
    for (sf_count_t j = 0; j < num_read; j++)
      channel_data[num_frames_written + j] = src[channels * j + i];
  }
  return num_frames_written + num_read;
}

Sample::Sample(AudioFormat format, uint32_t sample_rate)
    : format(format), sample_rate(sample_rate) {
}

Sample::Sample(Sample&& other) noexcept
    : name(std::move(other.name)),
      path(std::move(other.path)),
      format(std::exchange(other.format, AudioFormat::Unknown)),
      channels(std::exchange(other.channels, 0)),
      sample_rate(std::exchange(other.sample_rate, 0)),
      count(std::exchange(other.count, 0)),
      sample_data(std::move(other.sample_data)) {
}

Sample::~Sample() {
  for (auto sample : sample_data)
    std::free(sample);
}

void Sample::set_channel_count(uint32_t count) {
}

void Sample::reserve(size_t new_sample_count) {
}

void Sample::resize(size_t new_sample_count, uint32_t new_channels, bool discard) {
  assert(new_sample_count != 0);
  assert(new_channels != 0);
  if (new_sample_count != count) {
    uint32_t sample_size = get_audio_format_size(format);
    size_t byte_size = new_sample_count * sample_size;
    sample_data.resize(new_channels);
    if (discard || count == 0) {
      for (auto& channel_data : sample_data) {
        if (channel_data)
          std::free(channel_data);
        channel_data = (std::byte*)std::malloc(byte_size);
        assert(channel_data && "Cannot allocate sample data");
      }
    } else {
      size_t old_byte_size = math::min(count, new_sample_count) * sample_size;
      for (uint32_t i = 0; i < new_channels; i++) {
        void* new_channel_data = std::malloc(byte_size);
        assert(new_channel_data && "Cannot allocate sample data");
        if (sample_data[i]) {
          std::memcpy(new_channel_data, sample_data[i], old_byte_size);
          std::free(sample_data[i]);
        }
        sample_data[i] = (std::byte*)new_channel_data;
      }
    }
    channels = new_channels;
    count = new_sample_count;
  } else if (new_channels < channels) {
    for (uint32_t i = new_channels; i < channels; i++)
      std::free(sample_data[i]);
    sample_data.resize(new_channels);
    channels = new_channels;
  } else if (new_channels > channels) {
    uint32_t sample_size = get_audio_format_size(format);
    size_t byte_size = new_sample_count * sample_size;
    sample_data.resize(new_channels);
    for (uint32_t i = channels; i < new_channels; i++)
      sample_data[i] = (std::byte*)std::malloc(byte_size);
    channels = new_channels;
  }
}

std::optional<Sample> Sample::load_file(const std::filesystem::path& path) noexcept {
  if (!std::filesystem::is_regular_file(path))
    return {};

  // Try libsndfile first for uncompressed formats (WAV, AIFF, etc.)
  std::string str_path = path.generic_string();
  SF_INFO info;
  SNDFILE* file = sf_open(str_path.c_str(), SFM_READ, &info);
  if (!file)
    return load_compressed_file(path);

  AudioFormat format = from_sf_format(info.format & SF_FORMAT_SUBMASK);
  if (format == AudioFormat::Unknown) {
    sf_close(file);
    return {};
  }

  uint32_t sample_size = get_audio_format_size(format);
  size_t data_size = (info.frames + sample_padding) * sample_size;
  Vector<std::byte*> data;
  data.reserve(info.channels);

  for (int i = 0; i < info.channels; i++) {
    std::byte* channel_data = (std::byte*)std::malloc(data_size);
    if (!channel_data) {
      for (auto allocated_data : data)
        std::free(allocated_data);
      sf_close(file);
      return {};
    }
    std::memset(channel_data, 0, data_size);
    data.push_back(channel_data);
  }

  sf_count_t buffer_len_per_channel = 1024;
  void* decode_buffer = std::malloc(buffer_len_per_channel * info.channels * sample_size);
  if (!decode_buffer) {
    for (auto allocated_data : data)
      std::free(allocated_data);
    sf_close(file);
    return {};
  }

  sf_count_t num_frames_read = 0;
  sf_count_t num_frames_written = 0;
  switch (format) {
    case AudioFormat::I8: {
      assert(false && "Not supported at the moment");
      break;
    }
    case AudioFormat::I16: {
      int16_t* buffer = (int16_t*)decode_buffer;
      while ((num_frames_read = sf_readf_short(file, buffer, buffer_len_per_channel)))
        num_frames_written =
            deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
      break;
    }
    case AudioFormat::I32: {
      int32_t* buffer = (int32_t*)decode_buffer;
      while ((num_frames_read = sf_readf_int(file, buffer, buffer_len_per_channel)))
        num_frames_written =
            deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
      break;
    }
    case AudioFormat::F32: {
      float* buffer = (float*)decode_buffer;
      while ((num_frames_read = sf_readf_float(file, buffer, buffer_len_per_channel)))
        num_frames_written =
            deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
      break;
    }
    case AudioFormat::F64: assert(false && "Not supported at the moment"); break;
    default: break;
  }

  std::free(decode_buffer);
  sf_close(file);

  std::optional<Sample> ret;
  ret.emplace(format, (uint32_t)info.samplerate);
  ret->name = path.filename().string();
  ret->path = path;
  ret->channels = info.channels;
  ret->count = info.frames;
  ret->sample_data = std::move(data);

  return ret;
}

std::optional<Sample> Sample::load_compressed_file(const std::filesystem::path& path) noexcept {
  auto decoded = codec::decode_audio_file(path, sample_padding);
  if (!decoded)
    return {};

  std::optional<Sample> ret;
  ret.emplace(decoded->format, decoded->sample_rate);
  ret->name = path.filename().string();
  ret->path = path;
  ret->channels = decoded->channels;
  ret->count = decoded->total_samples;
  ret->sample_data = std::move(decoded->channel_data);

  return ret;
}

std::optional<SampleInfo> Sample::get_file_info(const std::filesystem::path& path) noexcept {
  auto info = codec::get_audio_file_info(path);
  if (!info)
    return {};

  return SampleInfo{
    .sample_count = info->sample_count,
    .channel_count = info->channel_count,
    .rate = info->sample_rate
  };
}

}  // namespace wb