#include "codec.h"

#include <algorithm>

#include "core/defer.h"
#include "extern/dr_mp3.h"
#include "flac_decoder.h"
#include "mp3_decoder.h"
#include "ogg_decoder.h"
#include "vorbis/vorbisfile.h"

namespace wb::codec {

std::optional<DecodedAudio> decode_audio_file(const std::filesystem::path& path, size_t padding) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".mp3") {
    return decode_mp3_file(path);
  }

  if (ext == ".flac") {
    return decode_flac_file(path, padding);
  }

  if (ext == ".ogg") {
    return decode_ogg_vorbis_file(path);
  }

  return {};
}

std::optional<AudioFileInfo> get_audio_file_info(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  SF_INFO sf_info{};
  std::u8string str_path = path.generic_u8string();
  SNDFILE* file = sf_open((const char*)str_path.c_str(), SFM_READ, &sf_info);
  if (file) {
    sf_close(file);
    return AudioFileInfo{
      .sample_count = (uint64_t)sf_info.frames,
      .channel_count = (uint32_t)sf_info.channels,
      .sample_rate = (uint32_t)sf_info.samplerate,
    };
  }

  drmp3 mp3;
  if (drmp3_init_file(&mp3, (const char*)str_path.c_str(), nullptr)) {
    defer(drmp3_uninit(&mp3));
    return AudioFileInfo{
      .sample_count = drmp3_get_pcm_frame_count(&mp3),
      .channel_count = mp3.channels,
      .sample_rate = mp3.sampleRate,
    };
  }

  OggVorbis_File vf;
  if (ov_fopen((const char*)str_path.c_str(), &vf) == 0) {
    defer(ov_clear(&vf));
    return AudioFileInfo{
      .sample_count = (uint64_t)ov_pcm_total(&vf, -1),
      .channel_count = (uint32_t)vf.vi->channels,
      .sample_rate = (uint32_t)vf.vi->rate,
    };
  }

  if (auto flac_info = get_flac_file_info(path)) {
    return AudioFileInfo{ .sample_count = flac_info->total_samples,
                          .channel_count = flac_info->channels,
                          .sample_rate = flac_info->sample_rate };
  }

  return {};
}

AudioSFEncoder::AudioSFEncoder(uint32_t file_format, AudioFormat sample_format)
    : file_format_(file_format),
      sample_format_(sample_format) {
}

AudioSFEncoder::~AudioSFEncoder() {
  close();
}

bool AudioSFEncoder::open(const char* file, uint32_t n_channels) {
  if (snd_file_)
    return false;

  SF_INFO info{};
  info.channels = n_channels;
  info.samplerate = 44100;

  switch (sample_format_) {
    case AudioFormat::I16: info.format |= SF_FORMAT_PCM_16; break;
    case AudioFormat::I24: info.format |= SF_FORMAT_PCM_24; break;
    case AudioFormat::I32: info.format |= SF_FORMAT_PCM_32; break;
    case AudioFormat::F32: info.format |= SF_FORMAT_FLOAT; break;
    default: return false;
  }

  switch (file_format_) {
    case AudioSFEncoder::WAV: info.format |= SF_FORMAT_WAV; break;
    case AudioSFEncoder::AIFF: info.format |= SF_FORMAT_AIFF; break;
  }

  snd_file_ = sf_open(file, SFM_WRITE, &info);
  if (!snd_file_)
    return false;

  return true;
}

void AudioSFEncoder::close() {
  if (snd_file_) {
    sf_close(snd_file_);
    snd_file_ = nullptr;
  }
}

size_t AudioSFEncoder::write(const float* data, uint32_t n_channels, uint32_t num_frames) {
  return sf_writef_float(snd_file_, data, num_frames);
}

AudioSFDecoder::~AudioSFDecoder() {
  close();
}

bool AudioSFDecoder::open(const char* file) {
  if (snd_file)
    return false;

  snd_file = sf_open(file, SFM_READ, &info);
  if (!snd_file)
    return false;

  switch (info.format & SF_FORMAT_SUBMASK) {
    case SF_FORMAT_PCM_16: format = AudioFormat::I16; break;
    case SF_FORMAT_PCM_24: format = AudioFormat::I32; break;
    case SF_FORMAT_PCM_32: format = AudioFormat::I32; break;
    case SF_FORMAT_FLOAT: format = AudioFormat::F32; break;
    default: sf_close(snd_file); return false;
  }

  channels = info.channels;

  return true;
}

void AudioSFDecoder::close() {
  if (snd_file) {
    sf_close(snd_file);
    snd_file = nullptr;
  }
}

size_t AudioSFDecoder::read_i16(int16_t* data, uint32_t n_channels, uint32_t num_frames) {
  return sf_readf_short(snd_file, data, num_frames);
}

size_t AudioSFDecoder::read_i32(int32_t* data, uint32_t n_channels, uint32_t num_frames) {
  return sf_readf_int(snd_file, data, num_frames);
}

size_t AudioSFDecoder::read_f32(float* data, uint32_t n_channels, uint32_t num_frames) {
  return sf_readf_float(snd_file, data, num_frames);
}

}  // namespace wb::codec