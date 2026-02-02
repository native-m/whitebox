#pragma once

#include <sndfile.h>

#include <filesystem>
#include <optional>

#include "core/audio_format.h"
#include "core/vector.h"

namespace wb::codec {

struct DecodedAudio {
  AudioFormat format{};
  uint32_t channels{};
  uint32_t sample_rate{};
  uint64_t total_samples{};
  Vector<std::byte*> channel_data{};
};

struct AudioFileInfo {
  uint64_t sample_count;
  uint32_t channel_count;
  uint32_t sample_rate;
};

std::optional<DecodedAudio> decode_audio_file(const std::filesystem::path& path, size_t padding = 0);

std::optional<AudioFileInfo> get_audio_file_info(const std::filesystem::path& path);

struct AudioEncoder {
  virtual ~AudioEncoder() {
  }
  virtual bool open(const char* file, uint32_t n_channels) = 0;
  virtual void close() = 0;
  virtual size_t write(const float* data, uint32_t n_channels, uint32_t num_frames) = 0;
};

struct AudioDecoder {
  AudioFormat format{};
  uint32_t channels{};

  virtual ~AudioDecoder() {
  }
  virtual bool open(const char* file) = 0;
  virtual void close() = 0;
  virtual size_t read_i16(int16_t* data, uint32_t n_channels, uint32_t num_frames) = 0;
  virtual size_t read_i32(int32_t* data, uint32_t n_channels, uint32_t num_frames) = 0;
  virtual size_t read_f32(float* data, uint32_t n_channels, uint32_t num_frames) = 0;
};

struct AudioSFEncoder final : public AudioEncoder {
  enum {
    WAV,
    AIFF,
  };

  SNDFILE* snd_file_{};
  uint32_t file_format_{};
  AudioFormat sample_format_{};

  AudioSFEncoder(uint32_t file_format, AudioFormat sample_format);
  ~AudioSFEncoder();
  bool open(const char* file, uint32_t n_channels) override;
  void close() override;
  size_t write(const float* data, uint32_t n_channels, uint32_t num_frames) override;
};

struct AudioSFDecoder final : public AudioDecoder {
  SF_INFO info{};
  SNDFILE* snd_file{};

  ~AudioSFDecoder();
  bool open(const char* file) override;
  void close() override;
  size_t read_i16(int16_t* data, uint32_t n_channels, uint32_t num_frames) override;
  size_t read_i32(int32_t* data, uint32_t n_channels, uint32_t num_frames) override;
  size_t read_f32(float* data, uint32_t n_channels, uint32_t num_frames) override;
};

}  // namespace wb::codec