#include "flac_decoder.h"

#include "FLAC/stream_decoder.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/defer.h"

namespace wb::codec {

struct FlacClientData {
  Vector<std::byte*> channel_data;
  uint64_t total_samples = 0;
  uint32_t sample_rate = 0;
  uint32_t channels = 0;
  uint32_t bits_per_sample = 0;
  AudioFormat format = AudioFormat::Unknown;
  uint64_t frames_decoded = 0;
  size_t padding = 0;
  bool error = false;

  ~FlacClientData() {
    if (error) {
      for (auto ptr : channel_data)
        std::free(ptr);
    }
  }
};

static FLAC__StreamDecoderWriteStatus flac_write_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data) {
  auto* data = (FlacClientData*)client_data;
  if (data->error)
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

  if (data->frames_decoded + frame->header.blocksize > data->total_samples) {
    data->error = true;
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }

  for (uint32_t c = 0; c < data->channels; c++) {
    const FLAC__int32* src_channel = buffer[c];

    if (data->format == AudioFormat::I16) {
      int16_t* dst = (int16_t*)data->channel_data[c];
      dst += data->frames_decoded;
      for (unsigned i = 0; i < frame->header.blocksize; i++) {
        dst[i] = (int16_t)src_channel[i];
      }
    } else if (data->format == AudioFormat::I32) {
      int32_t* dst = (int32_t*)data->channel_data[c];
      dst += data->frames_decoded;

      if (data->bits_per_sample == 24) {
        for (unsigned i = 0; i < frame->header.blocksize; i++) {
          dst[i] = (int32_t)(src_channel[i] << 8);
        }
      } else {
        for (unsigned i = 0; i < frame->header.blocksize; i++) {
          dst[i] = (int32_t)src_channel[i];
        }
      }
    }
  }

  data->frames_decoded += frame->header.blocksize;
  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
flac_metadata_callback(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data) {
  auto* data = (FlacClientData*)client_data;

  if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    data->total_samples = metadata->data.stream_info.total_samples;
    data->sample_rate = metadata->data.stream_info.sample_rate;
    data->channels = metadata->data.stream_info.channels;
    data->bits_per_sample = metadata->data.stream_info.bits_per_sample;

    if (data->bits_per_sample <= 16) {
      data->format = AudioFormat::I16;
    } else {
      data->format = AudioFormat::I32;
    }

    uint32_t sample_size = get_audio_format_size(data->format);
    size_t byte_size = (data->total_samples + data->padding) * sample_size;

    data->channel_data.reserve(data->channels);
    for (uint32_t i = 0; i < data->channels; i++) {
      std::byte* mem = (std::byte*)std::malloc(byte_size);
      if (!mem) {
        data->error = true;
        return;
      }
      std::memset(mem, 0, byte_size);
      data->channel_data.push_back(mem);
    }
  }
}

static void
flac_error_callback(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data) {
  ((FlacClientData*)client_data)->error = true;
}

std::optional<DecodedAudio> decode_flac_file(const std::filesystem::path& path, size_t padding) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  FLAC__StreamDecoder* decoder = FLAC__stream_decoder_new();
  if (!decoder)
    return {};

  defer(FLAC__stream_decoder_delete(decoder));

  FlacClientData client_data;
  client_data.padding = padding;

  FLAC__stream_decoder_set_md5_checking(decoder, true);

  FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_file(
      decoder, path.string().c_str(), flac_write_callback, flac_metadata_callback, flac_error_callback, &client_data);

  if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
    return {};
  }

  FLAC__bool success = FLAC__stream_decoder_process_until_end_of_stream(decoder);

  if (!success || client_data.error || client_data.channel_data.empty()) {
    return {};
  }

  return DecodedAudio{
    .format = client_data.format,
    .channels = client_data.channels,
    .sample_rate = client_data.sample_rate,
    .total_samples = client_data.total_samples,
    .channel_data = std::move(client_data.channel_data),
  };
}

std::optional<FlacFileInfo> get_flac_file_info(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path))
    return {};

  FLAC__StreamDecoder* decoder = FLAC__stream_decoder_new();
  if (!decoder)
    return {};

  defer(FLAC__stream_decoder_delete(decoder));

  FlacFileInfo info{};

  auto meta_cb = [](const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* d) {
    if (m->type == FLAC__METADATA_TYPE_STREAMINFO) {
      auto* info = (FlacFileInfo*)d;
      info->total_samples = m->data.stream_info.total_samples;
      info->channels = m->data.stream_info.channels;
      info->sample_rate = m->data.stream_info.sample_rate;
      info->bits_per_sample = m->data.stream_info.bits_per_sample;
    }
  };

  auto err_cb = [](const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) { };

  auto write_cb = [](const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*) {
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  };

  if (FLAC__stream_decoder_init_file(decoder, path.string().c_str(), write_cb, meta_cb, err_cb, &info) !=
      FLAC__STREAM_DECODER_INIT_STATUS_OK) {
    return {};
  }

  FLAC__stream_decoder_process_until_end_of_metadata(decoder);

  if (info.total_samples == 0)
    return {};

  return info;
}

}  // namespace wb::codec