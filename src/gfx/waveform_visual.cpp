#include "waveform_visual.h"

#include "core/debug.h"
#include "dsp/sample.h"
#include "renderer.h"

namespace wb {

template<typename T>
static void summarize_for_mipmaps_impl(
    AudioFormat sample_format,
    size_t sample_count,
    const std::byte* sample_data,
    size_t chunk_count,
    size_t block_count,
    size_t output_count,
    T* output_data) {
  switch (sample_format) {
    case AudioFormat::I8: {
      const int8_t* sample = (const int8_t*)sample_data;
      for (size_t i = 0; i < output_count; i += 2) {
        size_t idx = i * block_count;
        size_t chunk_length = std::min(chunk_count, sample_count - idx);
        T min_val = std::numeric_limits<T>::max();
        T max_val = std::numeric_limits<T>::min();
        size_t min_idx = 0;
        size_t max_idx = 0;

        static constexpr float conv_div_min =
            (float)std::numeric_limits<T>::min() / (float)std::numeric_limits<int8_t>::min();
        static constexpr float conv_div_max =
            (float)std::numeric_limits<T>::max() / (float)std::numeric_limits<int8_t>::max();
        const int8_t* chunk = &sample[i * block_count];
        for (size_t j = 0; j < chunk_length; j++) {
          float conv = (float)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
          T value = (T)conv;

          if (value < min_val) {
            min_val = value;
            min_idx = j;
          }
          if (value > max_val) {
            max_val = value;
            max_idx = j;
          }
        }

        if (max_idx < min_idx) {
          output_data[i] = max_val;
          output_data[i + 1] = min_val;
        } else {
          output_data[i] = min_val;
          output_data[i + 1] = max_val;
        }
      }
      break;
    }
    case AudioFormat::I16: {
      const int16_t* sample = (const int16_t*)sample_data;
      for (size_t i = 0; i < output_count; i += 2) {
        size_t idx = i * block_count;
        size_t chunk_length = std::min(chunk_count, sample_count - idx);
        T min_val = std::numeric_limits<T>::max();
        T max_val = std::numeric_limits<T>::min();
        size_t min_idx = 0;
        size_t max_idx = 0;

        static constexpr float conv_div_min =
            (float)std::numeric_limits<T>::min() / (float)std::numeric_limits<int16_t>::min();
        static constexpr float conv_div_max =
            (float)std::numeric_limits<T>::max() / (float)std::numeric_limits<int16_t>::max();
        const int16_t* chunk = &sample[i * block_count];
        for (size_t j = 0; j < chunk_length; j++) {
          float conv = (float)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
          T value = (T)conv;

          if (value < min_val) {
            min_val = value;
            min_idx = j;
          }
          if (value > max_val) {
            max_val = value;
            max_idx = j;
          }
        }

        if (max_idx < min_idx) {
          output_data[i] = max_val;
          output_data[i + 1] = min_val;
        } else {
          output_data[i] = min_val;
          output_data[i + 1] = max_val;
        }
      }
      break;
    }
    case AudioFormat::I32: {
      const int32_t* sample = (const int32_t*)sample_data;
      for (size_t i = 0; i < output_count; i += 2) {
        size_t idx = i * block_count;
        size_t chunk_length = std::min(chunk_count, sample_count - idx);
        T min_val = std::numeric_limits<T>::max();
        T max_val = std::numeric_limits<T>::min();
        size_t min_idx = 0;
        size_t max_idx = 0;

        static constexpr double conv_div_min =
            (double)std::numeric_limits<T>::min() / (double)std::numeric_limits<int32_t>::min();
        static constexpr double conv_div_max =
            (double)std::numeric_limits<T>::max() / (double)std::numeric_limits<int32_t>::max();
        const int32_t* chunk = &sample[i * block_count];
        for (size_t j = 0; j < chunk_length; j++) {
          double conv = (double)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
          T value = (T)conv;

          if (value < min_val) {
            min_val = value;
            min_idx = j;
          }
          if (value > max_val) {
            max_val = value;
            max_idx = j;
          }
        }

        if (max_idx < min_idx) {
          output_data[i] = max_val;
          output_data[i + 1] = min_val;
        } else {
          output_data[i] = min_val;
          output_data[i + 1] = max_val;
        }
      }
      break;
    }
    case AudioFormat::F32: {
      const float* sample = (const float*)sample_data;
      for (size_t i = 0; i < output_count; i += 2) {
        size_t idx = i * block_count;
        size_t chunk_length = std::min(chunk_count, sample_count - idx);
        T min_val = std::numeric_limits<T>::max();
        T max_val = std::numeric_limits<T>::min();
        size_t min_idx = 0;
        size_t max_idx = 0;

        const float* chunk = &sample[i * block_count];
        for (size_t j = 0; j < chunk_length; j++) {
          float conv = (float)chunk[j] * (chunk[j] >= 0.0f ? std::numeric_limits<T>::max() : -std::numeric_limits<T>::min());

          T value = (T)conv;
          if (value < min_val) {
            min_val = value;
            min_idx = j;
          }
          if (value > max_val) {
            max_val = value;
            max_idx = j;
          }
        }

        if (max_idx < min_idx) {
          output_data[i] = max_val;
          output_data[i + 1] = min_val;
        } else {
          output_data[i] = min_val;
          output_data[i + 1] = max_val;
        }
      }
      break;
    }
    default: break;
  }
}

WaveformVisual::~WaveformVisual() {
  for (auto& mipmap : mipmaps) {
    g_renderer->destroy_buffer(mipmap.data);
  }
}

WaveformVisual* WaveformVisual::create(Sample* sample, WaveformVisualQuality quality) {
  Vector<WaveformMipmap> mipmaps;
  size_t sample_count = sample->count;
  uint32_t current_mip = 1;
  uint32_t elem_size = 0;
  uint32_t max_mip = 0;

  switch (quality) {
    case WaveformVisualQuality::Low: elem_size = sizeof(int8_t); break;
    case WaveformVisualQuality::High: elem_size = sizeof(int16_t); break;
    default: WB_UNREACHABLE();
  }

  while (sample_count > 64) {
    size_t chunk_count = 1ull << current_mip;
    size_t block_count = 1ull << (current_mip - 1);
    size_t mip_data_count = sample->count / block_count;
    mip_data_count += mip_data_count % 2;

    Log::info("Generating mip-map {} ({})", current_mip, sample_count);

    size_t total_count = mip_data_count * sample->channels;
    size_t buffer_size = total_count * elem_size;
    GPUBuffer* buffer = g_renderer->create_buffer(GPUBufferUsage::Storage, buffer_size, false);
    assert(buffer && "Cannot create buffer");

    // Upload/copy peak data to the buffer
    void* upload_ptr = g_renderer->begin_upload_data(buffer, buffer_size);
    switch (quality) {
      case WaveformVisualQuality::Low:
        for (uint32_t i = 0; i < sample->channels; i++) {
          size_t offset = mip_data_count * (size_t)i;
          std::byte* data = sample->sample_data[i];
          summarize_for_mipmaps_impl(
              sample->format, sample->count, data, chunk_count, block_count, mip_data_count, (int8_t*)upload_ptr + offset);
        }
        break;
      case WaveformVisualQuality::High:
        for (uint32_t i = 0; i < sample->channels; i++) {
          size_t offset = mip_data_count * (size_t)i;
          std::byte* data = sample->sample_data[i];
          summarize_for_mipmaps_impl(
              sample->format, sample->count, data, chunk_count, block_count, mip_data_count, (int16_t*)upload_ptr + offset);
        }
        break;
    }
    g_renderer->end_upload_data();

    mipmaps.push_back({
      .data = buffer,
      .count = (uint32_t)mip_data_count,
    });

    sample_count /= 4;
    current_mip += 2;
    max_mip = current_mip - 1;
  }

  WaveformVisual* ret = new WaveformVisual();
  ret->sample_count = sample->count;
  ret->mipmap_count = mipmaps.size();
  ret->channels = sample->channels;
  ret->sample_rate = sample->sample_rate;
  ret->quality = quality;
  ret->cpu_accessible = false;
  ret->mipmaps = std::move(mipmaps);
  return ret;
}

void gfx_draw_waveform(const WaveformDrawCmd& command) {
}

void gfx_draw_waveform_batch(
    const Vector<WaveformDrawCmd>& commands,
    int32_t clip_x0,
    int32_t clip_y0,
    int32_t clip_x1,
    int32_t clip_y1) {
  if (commands.size() == 0)
    return;

  float fb_width = (float)(clip_x1 - clip_x0);
  float fb_height = (float)(clip_y1 - clip_y0);
  float vp_width = 2.0f / fb_width;
  float vp_height = 2.0f / fb_height;

  g_renderer->set_viewport((float)clip_x0, (float)clip_y0, fb_width, fb_height);

  for (auto& cmd : commands) {
    if (cmd.draw_count == 0)
      continue;
    if (cmd.min_x >= fb_width || cmd.max_x < 0.0f)
      continue;
    if (cmd.min_y >= fb_height || cmd.max_y < 0.0f)
      continue;

    WaveformMipmap& mip = cmd.waveform_vis->mipmaps[cmd.mip_index];
    int32_t x0 = std::max((int32_t)cmd.min_x, clip_x0);
    int32_t y0 = std::max((int32_t)cmd.min_y, clip_y0);
    int32_t x1 = std::min((int32_t)cmd.max_x, clip_x1);
    int32_t y1 = std::min((int32_t)cmd.max_y, clip_y1);
    uint32_t vertex_count = cmd.draw_count * 2;

    WaveformDrawParam draw_cmd{
      .origin_x = cmd.min_x + 0.5f,
      .origin_y = cmd.min_y,
      .scale_x = cmd.scale_x,
      .scale_y = cmd.max_y - cmd.min_y,
      .gain = cmd.gain,
      .vp_width = vp_width,
      .vp_height = vp_height,
      .gap_size = cmd.gap_size,
      .is_min = 0,
      .color = cmd.color,
      .channel = cmd.channel,
      .start_idx = cmd.start_idx,
      .sample_count = mip.count,
    };

    g_renderer->set_scissor(x0, y0, x1 - x0, y1 - y0);
    g_renderer->bind_storage_buffer(0, mip.data);

    // Draw filling
    g_renderer->bind_pipeline(g_renderer->waveform_fill);
    g_renderer->set_shader_parameter(sizeof(draw_cmd), &draw_cmd);
    g_renderer->draw(vertex_count, 0);

    // Draw anti-aliasing fringe (maximum part)
    g_renderer->bind_pipeline(g_renderer->waveform_aa);
    g_renderer->draw(vertex_count * 3, 0);

    // Draw anti-aliasing fringe (minimum part)
    draw_cmd.is_min = 1;
    g_renderer->set_shader_parameter(sizeof(draw_cmd), &draw_cmd);
    g_renderer->draw(vertex_count * 3, 0);
  }
}

}  // namespace wb