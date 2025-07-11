#include "codec.h"

namespace wb::dsp {

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
  switch (sample_format_) {
    case AudioFormat::I16: info.format |= SF_FORMAT_PCM_16; break;
    case AudioFormat::I24: info.format |= SF_FORMAT_PCM_24; break;
    case AudioFormat::I32: info.format |= SF_FORMAT_PCM_32; break;
    case AudioFormat::F32: info.format |= SF_FORMAT_FLOAT; break;
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

// --------------------------------------------------------------------------------------------------

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

}  // namespace wb::dsp