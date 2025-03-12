#pragma once

#include "core/common.h"
#include "core/vector.h"

namespace wb {
struct Sample;
struct WaveformVisual;
struct GPUBuffer;

enum class WaveformVisualQuality {
  Low,
  High,
};

struct WaveformDrawCmd {
  WaveformVisual* waveform_vis;
  float min_x;
  float min_y;
  float max_x;
  float max_y;
  float gain;
  float scale_x;
  uint32_t color;
  int32_t mip_index;
  uint32_t channel;
  uint32_t start_idx;
  uint32_t draw_count;
};

struct WaveformDrawParam {
  float origin_x;
  float origin_y;
  float scale_x;
  float scale_y;
  float gain;
  float vp_width;
  float vp_height;
  int is_min;
  uint32_t color;
  uint32_t channel;
  uint32_t start_idx;
  uint32_t sample_count;
};

struct WaveformMipmap {
  GPUBuffer* data;
  uint32_t count;
};

struct WaveformVisual {
  size_t sample_count;
  int32_t mipmap_count;
  int32_t channels;
  int32_t sample_rate;
  WaveformVisualQuality quality;
  bool cpu_accessible;
  Vector<WaveformMipmap> mipmaps;
  ~WaveformVisual();

  static WaveformVisual* create(Sample* sample, WaveformVisualQuality quality);
};

void gfx_draw_waveform(const WaveformDrawCmd& command);
void gfx_draw_waveform_batch(
    const Vector<WaveformDrawCmd>& commands,
    int32_t clip_x0,
    int32_t clip_y0,
    int32_t clip_x1,
    int32_t clip_y1);

}  // namespace wb